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
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ziliu::broker {
namespace {

constexpr int kRimeBackspace = 0xFF08;

bool FilesHaveSameContents(const std::filesystem::path& source,
                           const std::filesystem::path& destination) {
  std::error_code file_error;
  const auto source_size = std::filesystem::file_size(source, file_error);
  if (file_error) {
    return false;
  }
  const auto destination_size = std::filesystem::file_size(destination, file_error);
  if (file_error || source_size != destination_size) {
    return false;
  }

  std::ifstream source_stream(source, std::ios::binary);
  std::ifstream destination_stream(destination, std::ios::binary);
  if (!source_stream || !destination_stream) {
    return false;
  }
  return std::equal(std::istreambuf_iterator<char>(source_stream),
                    std::istreambuf_iterator<char>(),
                    std::istreambuf_iterator<char>(destination_stream));
}

bool CopyFileIfDifferent(const std::filesystem::path& source,
                         const std::filesystem::path& destination) {
  if (FilesHaveSameContents(source, destination)) {
    return true;
  }
  std::error_code copy_error;
  return std::filesystem::copy_file(source, destination,
                                    std::filesystem::copy_options::overwrite_existing,
                                    copy_error) &&
         !copy_error;
}

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

std::wstring FormatPreedit(std::wstring_view preedit) {
  std::wstring result;
  result.reserve(preedit.size());
  bool pending_separator = false;
  for (const wchar_t character : preedit) {
    const bool whitespace =
        character == L' ' || character == L'\t' || character == L'\r' || character == L'\n';
    if (whitespace) {
      pending_separator = !result.empty();
      continue;
    }
    if (pending_separator && result.back() != L'\'' && character != L'\'') {
      result.push_back(L'\'');
    }
    pending_separator = false;
    result.push_back(character);
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
      if (!CopyFileIfDifferent(shared_data_path / overlay, user_data_path / overlay)) {
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
    // Full maintenance rebuilds the Rime workspace on every Broker cold start.
    // The non-full path checks source timestamps and only schedules deployment
    // after an install, bundled-data update, or user configuration change.
    if (api_->start_maintenance(False)) {
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
    ResetPaging();
    ClearTrackedInput();
    api_->clear_composition(session_id_);
  }

  bool ProcessLetter(wchar_t letter) override {
    if (letter < L'A' || (letter > L'Z' && letter < L'a') || letter > L'z') {
      return false;
    }
    const int keycode = static_cast<int>(letter >= L'A' && letter <= L'Z' ? letter - L'A' + L'a'
                                                                          : letter);
    if (pinyin_letter_count_ >= core::kMaximumPinyinLetters) {
      ResetPaging();
      return true;
    }
    const bool consumed = ProcessTrackedKey(keycode, static_cast<wchar_t>(keycode), true);
    if (consumed) {
      ResetPaging();
    }
    return consumed;
  }

  bool ProcessSeparator() override {
    if (!raw_keys_.empty() && raw_keys_.back() == L'\'') {
      return false;
    }
    const bool consumed = ProcessTrackedKey('\'', L'\'', false);
    if (consumed) {
      ResetPaging();
    }
    return consumed;
  }

  bool Backspace() override {
    if (raw_keys_.empty()) {
      return false;
    }
    if (!automatic_commit_text_prefix_.empty()) {
      raw_keys_.pop_back();
      RebuildComposition();
      ResetPaging();
      return true;
    }
    const bool consumed = api_->process_key(session_id_, kRimeBackspace, 0) != False;
    if (consumed) {
      if (raw_keys_.back() != L'\'') {
        --pinyin_letter_count_;
      }
      raw_keys_.pop_back();
      ResetPaging();
    }
    return consumed;
  }

  bool PageUp() override {
    if (pinyin_letter_count_ >= core::kMaximumPinyinLetters) {
      return false;
    }
    if (previous_page_offsets_.empty()) {
      return false;
    }
    candidate_offset_ = previous_page_offsets_.back();
    previous_page_offsets_.pop_back();
    return true;
  }

  bool PageDown() override {
    if (pinyin_letter_count_ >= core::kMaximumPinyinLetters) {
      return false;
    }
    const int next_offset = NextVisiblePageOffset();
    if (next_offset < 0) {
      return false;
    }
    previous_page_offsets_.push_back(candidate_offset_);
    candidate_offset_ = next_offset;
    return true;
  }

  void SetCandidatePageSize(std::size_t page_size) override {
    ResetPaging();
    candidate_page_size_ = std::clamp<std::size_t>(page_size, 1, core::ipc::kMaximumCandidates);
  }

  void SetTraditional(bool enabled) override {
    ResetPaging();
    api_->set_option(session_id_, "traditionalization", enabled ? True : False);
  }

  void SetChineseCandidatesOnly(bool enabled) override {
    if (chinese_candidates_only_ == enabled) {
      return;
    }
    chinese_candidates_only_ = enabled;
    ResetPaging();
  }

  core::SelectionResult Select(std::size_t candidate_index) override {
    if (pinyin_letter_count_ >= core::kMaximumPinyinLetters) {
      return {};
    }
    const int source_candidate_index = CandidateIndexForVisible(candidate_index);
    if (candidate_index >= candidate_page_size_ || source_candidate_index < 0 ||
        !api_->select_candidate(session_id_,
                                static_cast<std::size_t>(source_candidate_index))) {
      return {};
    }
    ResetPaging();

    RIME_STRUCT(RimeCommit, commit);
    if (api_->get_commit(session_id_, &commit)) {
      std::wstring result = automatic_commit_text_prefix_ + FromUtf8(commit.text);
      api_->free_commit(&commit);
      ClearTrackedInput();
      return core::SelectionResult{true, std::move(result)};
    }

    RIME_STRUCT(RimeContext, context);
    const bool has_context = api_->get_context(session_id_, &context);
    const bool selection_is_complete =
        has_context && context.composition.length > 0 && context.menu.num_candidates == 0;
    if (has_context) {
      api_->free_context(&context);
    }
    if (!selection_is_complete || !api_->commit_composition(session_id_)) {
      return core::SelectionResult{true, {}};
    }
    if (!api_->get_commit(session_id_, &commit)) {
      return core::SelectionResult{true, {}};
    }
    std::wstring result = automatic_commit_text_prefix_ + FromUtf8(commit.text);
    api_->free_commit(&commit);
    ClearTrackedInput();
    return core::SelectionResult{true, std::move(result)};
  }

  [[nodiscard]] core::CompositionSnapshot Snapshot() const override {
    core::CompositionSnapshot snapshot = ReadSnapshot();
    if (!automatic_commit_preedit_prefix_.empty()) {
      if (!snapshot.preedit.empty() && automatic_commit_preedit_prefix_.back() != L'\'' &&
          snapshot.preedit.front() != L'\'') {
        snapshot.preedit.insert(snapshot.preedit.begin(), L'\'');
      }
      snapshot.preedit.insert(0, automatic_commit_preedit_prefix_);
    }
    if (!automatic_commit_text_prefix_.empty()) {
      for (auto& candidate : snapshot.candidates) {
        candidate.text.insert(0, automatic_commit_text_prefix_);
      }
    }
    if (pinyin_letter_count_ >= core::kMaximumPinyinLetters) {
      snapshot.candidates.clear();
    }
    return snapshot;
  }

 private:
  bool ProcessTrackedKey(int keycode, wchar_t raw_key, bool is_letter) {
    const core::CompositionSnapshot before = ReadSnapshot();
    if (api_->process_key(session_id_, keycode, 0) == False) {
      return false;
    }
    raw_keys_.push_back(raw_key);
    if (is_letter) {
      ++pinyin_letter_count_;
    }

    RIME_STRUCT(RimeCommit, commit);
    if (api_->get_commit(session_id_, &commit)) {
      automatic_commit_text_prefix_ += FromUtf8(commit.text);
      api_->free_commit(&commit);
      if (!before.preedit.empty()) {
        if (!automatic_commit_preedit_prefix_.empty() &&
            automatic_commit_preedit_prefix_.back() != L'\'' &&
            before.preedit.front() != L'\'') {
          automatic_commit_preedit_prefix_.push_back(L'\'');
        }
        automatic_commit_preedit_prefix_ += before.preedit;
      }
    }
    return true;
  }

  void RebuildComposition() {
    const std::wstring keys = raw_keys_;
    api_->clear_composition(session_id_);
    ClearTrackedInput();
    for (const wchar_t key : keys) {
      const bool is_letter = key != L'\'';
      if (!ProcessTrackedKey(static_cast<int>(key), key, is_letter)) {
        break;
      }
    }
  }

  void ClearTrackedInput() {
    raw_keys_.clear();
    automatic_commit_text_prefix_.clear();
    automatic_commit_preedit_prefix_.clear();
    pinyin_letter_count_ = 0;
  }

  [[nodiscard]] core::CompositionSnapshot ReadSnapshot() const {
    core::CompositionSnapshot snapshot;
    RIME_STRUCT(RimeContext, context);
    if (!api_->get_context(session_id_, &context)) {
      return snapshot;
    }
    snapshot.preedit = FormatPreedit(FromUtf8(context.composition.preedit));
    const bool has_menu = context.menu.num_candidates > 0;
    api_->free_context(&context);

    if (has_menu) {
      RimeCandidateListIterator iterator{};
      if (api_->candidate_list_from_index(session_id_, &iterator, candidate_offset_)) {
        snapshot.candidates.reserve(candidate_page_size_);
        while (snapshot.candidates.size() < candidate_page_size_ &&
               api_->candidate_list_next(&iterator)) {
          const auto& candidate = iterator.candidate;
          const std::wstring candidate_text = FromUtf8(candidate.text);
          if (!ShouldShowCandidate(candidate_text)) {
            continue;
          }
          const std::size_t visible_index = snapshot.candidates.size();
          snapshot.candidates.push_back(core::Candidate{
              candidate_text, FromUtf8(candidate.comment),
              1.0 - static_cast<double>(visible_index) /
                        static_cast<double>(candidate_page_size_ + 1)});
        }
        api_->candidate_list_end(&iterator);
      }
    }
    return snapshot;
  }

  void ResetPaging() {
    candidate_offset_ = 0;
    previous_page_offsets_.clear();
  }

  [[nodiscard]] bool ShouldShowCandidate(std::wstring_view text) const noexcept {
    return !chinese_candidates_only_ || core::IsChineseCandidate(text);
  }

  [[nodiscard]] int CandidateIndexForVisible(std::size_t visible_index) const {
    if (visible_index >= candidate_page_size_) {
      return -1;
    }
    RimeCandidateListIterator iterator{};
    if (!api_->candidate_list_from_index(session_id_, &iterator, candidate_offset_)) {
      return -1;
    }
    int source_index = candidate_offset_;
    std::size_t current_visible_index = 0;
    int result = -1;
    while (api_->candidate_list_next(&iterator)) {
      const int current_source_index = source_index++;
      if (!ShouldShowCandidate(FromUtf8(iterator.candidate.text))) {
        continue;
      }
      if (current_visible_index == visible_index) {
        result = current_source_index;
        break;
      }
      ++current_visible_index;
    }
    api_->candidate_list_end(&iterator);
    return result;
  }

  [[nodiscard]] int NextVisiblePageOffset() const {
    RimeCandidateListIterator iterator{};
    if (!api_->candidate_list_from_index(session_id_, &iterator, candidate_offset_)) {
      return -1;
    }
    int source_index = candidate_offset_;
    std::size_t visible_count = 0;
    int next_offset = -1;
    while (api_->candidate_list_next(&iterator)) {
      const int current_source_index = source_index++;
      if (!ShouldShowCandidate(FromUtf8(iterator.candidate.text))) {
        continue;
      }
      if (visible_count == candidate_page_size_) {
        next_offset = current_source_index;
        break;
      }
      ++visible_count;
    }
    api_->candidate_list_end(&iterator);
    return next_offset;
  }

  RimeApi* api_;
  RimeSessionId session_id_;
  int candidate_offset_ = 0;
  std::size_t candidate_page_size_ = core::ipc::kMaximumCandidates;
  bool chinese_candidates_only_ = true;
  std::wstring raw_keys_;
  std::wstring automatic_commit_text_prefix_;
  std::wstring automatic_commit_preedit_prefix_;
  std::size_t pinyin_letter_count_ = 0;
  std::vector<int> previous_page_offsets_;
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

void WarmUpEngineRuntime() {
  static_cast<void>(RimeRuntime::Instance());
}

std::unique_ptr<core::Engine> CreateEngine() {
  auto engine = TryCreateRimeEngine();
  return engine != nullptr ? std::move(engine) : core::CreateStubEngine();
}

}  // namespace ziliu::broker
