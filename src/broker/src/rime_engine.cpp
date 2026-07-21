#include "ziliu/broker/rime_engine.h"

#include "ziliu/core/ipc_protocol.h"

#include <windows.h>
#include <shlobj.h>

#include <rime_api.h>

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace ziliu::broker {
namespace {

constexpr int kRimeBackspace = 0xFF08;

std::string ToUtf8(std::wstring_view value) {
  if (value.empty()) {
    return {};
  }
  const int required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                                           static_cast<int>(value.size()), nullptr, 0, nullptr,
                                           nullptr);
  if (required <= 0) {
    return {};
  }
  std::string result(static_cast<std::size_t>(required), '\0');
  if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                          static_cast<int>(value.size()), result.data(), required, nullptr,
                          nullptr) != required) {
    return {};
  }
  return result;
}

std::wstring FromUtf8(const char* value) {
  if (value == nullptr || *value == '\0') {
    return {};
  }
  const int length = static_cast<int>(std::strlen(value));
  const int required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value, length, nullptr, 0);
  if (required <= 0) {
    return {};
  }
  std::wstring result(static_cast<std::size_t>(required), L'\0');
  if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value, length, result.data(), required) !=
      required) {
    return {};
  }
  return result;
}

std::filesystem::path ExecutableDirectory() {
  std::wstring path(32768, L'\0');
  const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
  if (length == 0 || static_cast<std::size_t>(length) >= path.size()) {
    return {};
  }
  path.resize(length);
  return std::filesystem::path(path).parent_path();
}

std::filesystem::path UserDataDirectory() {
  std::wstring override_path(32768, L'\0');
  const DWORD override_length =
      GetEnvironmentVariableW(L"ZILIU_RIME_USER_DATA_DIR", override_path.data(),
                              static_cast<DWORD>(override_path.size()));
  if (override_length > 0 && static_cast<std::size_t>(override_length) < override_path.size()) {
    override_path.resize(override_length);
    return override_path;
  }

  PWSTR local_app_data = nullptr;
  if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr,
                                  &local_app_data))) {
    return {};
  }
  const std::filesystem::path result =
      std::filesystem::path(local_app_data) / L"Ziliu" / L"Rime";
  CoTaskMemFree(local_app_data);
  return result;
}

class RimeRuntime final {
 public:
  ~RimeRuntime() {
    if (api_ != nullptr) {
      api_->finalize();
    }
    if (module_ != nullptr) {
      FreeLibrary(module_);
    }
  }

  RimeRuntime(const RimeRuntime&) = delete;
  RimeRuntime& operator=(const RimeRuntime&) = delete;

  static RimeRuntime& Instance() {
    static RimeRuntime runtime;
    return runtime;
  }

  [[nodiscard]] RimeApi* api() const noexcept { return api_; }

 private:
  RimeRuntime() { Initialize(); }

  void Initialize() {
    const auto executable_directory = ExecutableDirectory();
    const auto shared_data_path = executable_directory / L"data" / L"rime";
    const auto library_path = executable_directory / L"rime.dll";
    std::error_code file_error;
    const bool has_library = std::filesystem::is_regular_file(library_path, file_error);
    file_error.clear();
    const bool has_data =
        std::filesystem::is_regular_file(shared_data_path / L"default.yaml", file_error);
    if (executable_directory.empty() || !has_library || !has_data || file_error) {
      return;
    }

    const auto user_data_path = UserDataDirectory();
    if (user_data_path.empty()) {
      return;
    }
    std::error_code directory_error;
    std::filesystem::create_directories(user_data_path, directory_error);
    if (directory_error) {
      return;
    }
    for (const auto* overlay : {L"default.custom.yaml", L"rime_ice.custom.yaml"}) {
      std::error_code copy_error;
      std::filesystem::copy_file(shared_data_path / overlay, user_data_path / overlay,
                                 std::filesystem::copy_options::overwrite_existing, copy_error);
      if (copy_error) {
        return;
      }
    }

    module_ = LoadLibraryExW(library_path.c_str(), nullptr,
                             LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (module_ == nullptr) {
      return;
    }
    using GetApi = RimeApi*(__cdecl*)();
    const FARPROC procedure = GetProcAddress(module_, "rime_get_api");
    if (procedure == nullptr) {
      FreeLibrary(module_);
      module_ = nullptr;
      return;
    }
    const auto get_api = std::bit_cast<GetApi>(procedure);
    api_ = get_api();
    if (!HasRequiredApi()) {
      api_ = nullptr;
      FreeLibrary(module_);
      module_ = nullptr;
      return;
    }

    shared_data_directory_ = ToUtf8(shared_data_path.native());
    user_data_directory_ = ToUtf8(user_data_path.native());
    if (shared_data_directory_.empty() || user_data_directory_.empty()) {
      api_ = nullptr;
      FreeLibrary(module_);
      module_ = nullptr;
      return;
    }

    RIME_STRUCT(RimeTraits, traits);
    traits.shared_data_dir = shared_data_directory_.c_str();
    traits.user_data_dir = user_data_directory_.c_str();
    traits.distribution_name = "Ziliu";
    traits.distribution_code_name = "ziliu";
    traits.distribution_version = "0.2.0-dev";
    traits.app_name = "rime.ziliu";
    traits.min_log_level = 2;
    api_->setup(&traits);
    api_->initialize(&traits);
    if (api_->start_maintenance(True)) {
      api_->join_maintenance_thread();
    }
  }

  [[nodiscard]] bool HasRequiredApi() const noexcept {
    return api_ != nullptr && api_->setup != nullptr && api_->initialize != nullptr &&
           api_->finalize != nullptr && api_->start_maintenance != nullptr &&
           api_->join_maintenance_thread != nullptr && api_->create_session != nullptr &&
           api_->destroy_session != nullptr && api_->process_key != nullptr &&
           api_->clear_composition != nullptr && api_->commit_composition != nullptr &&
           api_->get_commit != nullptr && api_->free_commit != nullptr &&
           api_->get_context != nullptr && api_->free_context != nullptr &&
           api_->select_schema != nullptr && api_->set_option != nullptr &&
           api_->select_candidate != nullptr && api_->candidate_list_from_index != nullptr &&
           api_->candidate_list_next != nullptr && api_->candidate_list_end != nullptr;
  }

  HMODULE module_ = nullptr;
  RimeApi* api_ = nullptr;
  std::string shared_data_directory_;
  std::string user_data_directory_;
};

class RimeEngine final : public core::Engine {
 public:
  RimeEngine(RimeApi* api, RimeSessionId session_id) : api_(api), session_id_(session_id) {}
  ~RimeEngine() override { api_->destroy_session(session_id_); }

  void Reset() override {
    candidate_offset_ = 0;
    api_->clear_composition(session_id_);
  }

  bool ProcessLetter(wchar_t letter) override {
    if (letter < L'A' || (letter > L'Z' && letter < L'a') || letter > L'z') {
      return false;
    }
    const int keycode = static_cast<int>(letter >= L'A' && letter <= L'Z' ? letter - L'A' + L'a'
                                                                          : letter);
    const bool consumed = api_->process_key(session_id_, keycode, 0) != False;
    if (consumed) {
      candidate_offset_ = 0;
    }
    return consumed;
  }

  bool Backspace() override {
    const bool consumed = api_->process_key(session_id_, kRimeBackspace, 0) != False;
    if (consumed) {
      candidate_offset_ = 0;
    }
    return consumed;
  }

  bool PageUp() override {
    if (candidate_offset_ == 0) {
      return false;
    }
    candidate_offset_ = std::max(candidate_offset_ - static_cast<int>(candidate_page_size_), 0);
    return true;
  }

  bool PageDown() override {
    const int page_size = static_cast<int>(candidate_page_size_);
    if (candidate_offset_ > std::numeric_limits<int>::max() - page_size) {
      return false;
    }
    const int next_offset = candidate_offset_ + page_size;
    if (!HasCandidateAt(next_offset)) {
      return false;
    }
    candidate_offset_ = next_offset;
    return true;
  }

  void SetCandidatePageSize(std::size_t page_size) override {
    candidate_offset_ = 0;
    candidate_page_size_ = std::clamp<std::size_t>(page_size, 1, core::ipc::kMaximumCandidates);
  }

  void SetTraditional(bool enabled) override {
    candidate_offset_ = 0;
    api_->set_option(session_id_, "traditionalization", enabled ? True : False);
  }

  std::wstring Select(std::size_t candidate_index) override {
    if (candidate_index >= candidate_page_size_ ||
        !api_->select_candidate(session_id_,
                                static_cast<std::size_t>(candidate_offset_) + candidate_index)) {
      return {};
    }
    static_cast<void>(api_->commit_composition(session_id_));
    RIME_STRUCT(RimeCommit, commit);
    if (!api_->get_commit(session_id_, &commit)) {
      return {};
    }
    std::wstring result = FromUtf8(commit.text);
    api_->free_commit(&commit);
    candidate_offset_ = 0;
    return result;
  }

  [[nodiscard]] core::CompositionSnapshot Snapshot() const override {
    core::CompositionSnapshot snapshot;
    RIME_STRUCT(RimeContext, context);
    if (!api_->get_context(session_id_, &context)) {
      return snapshot;
    }
    snapshot.preedit = FromUtf8(context.composition.preedit);
    const bool has_menu = context.menu.num_candidates > 0;
    api_->free_context(&context);

    if (has_menu) {
      RimeCandidateListIterator iterator{};
      if (api_->candidate_list_from_index(session_id_, &iterator, candidate_offset_)) {
        snapshot.candidates.reserve(candidate_page_size_);
        while (snapshot.candidates.size() < candidate_page_size_ &&
               api_->candidate_list_next(&iterator)) {
          const auto& candidate = iterator.candidate;
          const std::size_t visible_index = snapshot.candidates.size();
          snapshot.candidates.push_back(core::Candidate{
              FromUtf8(candidate.text), FromUtf8(candidate.comment),
              1.0 - static_cast<double>(visible_index) /
                        static_cast<double>(candidate_page_size_ + 1)});
        }
        api_->candidate_list_end(&iterator);
      }
    }
    return snapshot;
  }

 private:
  [[nodiscard]] bool HasCandidateAt(int candidate_index) const {
    RimeCandidateListIterator iterator{};
    if (!api_->candidate_list_from_index(session_id_, &iterator, candidate_index)) {
      return false;
    }
    const bool found = api_->candidate_list_next(&iterator) != False;
    api_->candidate_list_end(&iterator);
    return found;
  }

  RimeApi* api_;
  RimeSessionId session_id_;
  int candidate_offset_ = 0;
  std::size_t candidate_page_size_ = core::ipc::kMaximumCandidates;
};

std::unique_ptr<core::Engine> TryCreateRimeEngine() {
  RimeApi* api = RimeRuntime::Instance().api();
  if (api == nullptr) {
    return nullptr;
  }
  const RimeSessionId session_id = api->create_session();
  if (session_id == 0) {
    return nullptr;
  }
  if (!api->select_schema(session_id, "rime_ice")) {
    api->destroy_session(session_id);
    return nullptr;
  }
  return std::make_unique<RimeEngine>(api, session_id);
}

}  // namespace

std::unique_ptr<core::Engine> CreateEngine() {
  auto engine = TryCreateRimeEngine();
  return engine != nullptr ? std::move(engine) : core::CreateStubEngine();
}

}  // namespace ziliu::broker
