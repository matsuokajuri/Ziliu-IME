#include "ziliu/tsf/text_service.h"
#include "commit_caret.h"
#include "input_privacy.h"
#include "startup_key_queue.h"

#include "ziliu/core/ipc_protocol.h"
#include "ziliu/core/settings.h"
#include "ziliu/ipc/pipe_client.h"
#include "ziliu/tsf/language_bar_button.h"
#include "ziliu/tsf/module_state.h"
#include "ziliu/ui/candidate_window.h"

#include <shellapi.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ziliu::tsf {

// A newly created SearchHost composition can take longer than the default
// 100 ms IPC budget on its first Rime key. Keep a finite bound without
// dropping that first key into the search box as raw Latin text.
constexpr std::uint32_t kInputBrokerTimeoutMilliseconds = 500;

struct TextServiceState {
  ipc::PipeClient client{ipc::kBrokerPipeName, kInputBrokerTimeoutMilliseconds};
  std::uint64_t request_id = 1;
  std::uint64_t session_id = 0;
  core::ipc::Response pending_response;
  core::CompositionSnapshot snapshot;
  ui::CandidateWindow candidate_window;
  ITfLangBarItemMgr* language_bar_manager = nullptr;
  LanguageBarButton* language_bar_button = nullptr;
  RECT candidate_anchor{};
  HWND candidate_owner = nullptr;
  core::Settings settings;
  std::filesystem::file_time_type settings_write_time{};
  ULONGLONG remote_settings_probe_time = 0;
  std::size_t candidate_page_offset = 0;
  std::size_t pending_caret_back = 0;
  Microsoft::WRL::ComPtr<ITfRange> committed_pair_range;
  Microsoft::WRL::ComPtr<ITfContext> committed_pair_context;
  Microsoft::WRL::ComPtr<ITfContext> key_context;
  detail::InputPrivacy key_privacy = detail::InputPrivacy::kBlocked;
  Microsoft::WRL::ComPtr<ITfContext> startup_context;
  Microsoft::WRL::ComPtr<ITfRange> startup_selection;
  detail::StartupKeyQueue startup_keys;
  std::wstring committed_pair;
  HWND committed_pair_focus = nullptr;
  HWND committed_pair_foreground = nullptr;
  ULONGLONG committed_pair_time = 0;
  ULONGLONG startup_started_time = 0;
  HWND startup_window = nullptr;
  HWND startup_focus = nullptr;
  HWND startup_foreground = nullptr;
  bool committed_pair_needs_left = false;
  bool broker_started = false;
  bool settings_file_known = false;
  bool chinese_mode = true;
  bool switch_key_down = false;
  bool switch_key_used = false;
  bool opening_quote = true;
  bool publishing_input_mode = false;
  bool startup_timer_active = false;
  bool startup_replay_scheduled = false;
};

class CompositionEditSession final : public ITfEditSession {
 public:
  enum class StartupMode { kReplay, kFallback };

  CompositionEditSession(TextService* service, ITfContext* context, bool verify_caret = false)
      : service_(service), context_(context), verify_caret_(verify_caret) {
    service_->AddRef();
    context_->AddRef();
  }

  CompositionEditSession(TextService* service, ITfContext* context, WPARAM key, bool key_up)
      : CompositionEditSession(service, context) {
    process_key_ = true;
    key_ = key;
    key_up_ = key_up;
  }

  CompositionEditSession(TextService* service, ITfContext* context,
                         std::uint64_t startup_generation, StartupMode startup_mode)
      : CompositionEditSession(service, context) {
    startup_generation_ = startup_generation;
    startup_fallback_ = startup_mode == StartupMode::kFallback;
  }

  [[nodiscard]] BOOL eaten() const { return eaten_; }

  STDMETHODIMP QueryInterface(REFIID interface_id, void** object) override {
    if (object == nullptr) {
      return E_INVALIDARG;
    }
    *object = nullptr;
    if (IsEqualIID(interface_id, IID_IUnknown) ||
        IsEqualIID(interface_id, IID_ITfEditSession)) {
      *object = static_cast<ITfEditSession*>(this);
      AddRef();
      return S_OK;
    }
    return E_NOINTERFACE;
  }

  STDMETHODIMP_(ULONG) AddRef() override { return ++reference_count_; }

  STDMETHODIMP_(ULONG) Release() override {
    const ULONG count = --reference_count_;
    if (count == 0) {
      delete this;
    }
    return count;
  }

  STDMETHODIMP DoEditSession(TfEditCookie edit_cookie) override {
    if (startup_generation_ != 0) {
      return startup_fallback_
                 ? service_->FallbackStartupKeys(edit_cookie, context_, startup_generation_)
                 : service_->ReplayStartupKeys(edit_cookie, context_, startup_generation_);
    }
    if (process_key_) {
      return key_up_ ? service_->HandleKeyUp(edit_cookie, context_, key_, &eaten_)
                     : service_->HandleKeyDown(edit_cookie, context_, key_, &eaten_);
    }
    if (verify_caret_) {
      return service_->VerifyCommittedPairCaret(edit_cookie, context_);
    }
    return service_->ApplyCompositionEdit(edit_cookie, context_);
  }

 private:
  ~CompositionEditSession() {
    context_->Release();
    service_->Release();
  }

  std::atomic<ULONG> reference_count_{1};
  TextService* service_;
  ITfContext* context_;
  bool verify_caret_;
  bool process_key_ = false;
  bool key_up_ = false;
  WPARAM key_ = 0;
  BOOL eaten_ = FALSE;
  std::uint64_t startup_generation_ = 0;
  bool startup_fallback_ = false;
};

namespace {

constexpr wchar_t kStartupWindowClass[] = L"Ziliu.StartupReplayWindow.v1";
constexpr UINT_PTR kStartupTimer = 1;
constexpr UINT kStartupPollMilliseconds = 15;
constexpr ULONGLONG kStartupMaximumMilliseconds = 30000;
constexpr std::size_t kStartupReplayBatchSize = 8;

bool HasAltModifier() { return (GetKeyState(VK_MENU) & 0x8000) != 0; }

bool HasControlModifier() { return (GetKeyState(VK_CONTROL) & 0x8000) != 0; }

bool HasShiftModifier() { return (GetKeyState(VK_SHIFT) & 0x8000) != 0; }

std::optional<std::filesystem::path> SettingsPath() {
  std::wstring local_app_data(32768, L'\0');
  const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", local_app_data.data(),
                                               static_cast<DWORD>(local_app_data.size()));
  if (length == 0 || static_cast<std::size_t>(length) >= local_app_data.size()) {
    return std::nullopt;
  }
  local_app_data.resize(length);
  return std::filesystem::path(local_app_data) / L"Ziliu" / L"settings.ini";
}

std::optional<std::string> ReadSettingsFile(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    return std::nullopt;
  }
  return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

bool IsLetterKey(WPARAM key) { return key >= L'A' && key <= L'Z'; }

bool IsPageKey(WPARAM key, core::PageKeySet key_set, bool next) {
  if (key_set == core::PageKeySet::kSemicolonApostrophe) {
    return key == (next ? VK_OEM_7 : VK_OEM_1);
  }
  if (key_set == core::PageKeySet::kBrackets) {
    return key == (next ? VK_OEM_6 : VK_OEM_4);
  }
  return key == (next ? VK_OEM_PERIOD : VK_OEM_COMMA);
}

std::wstring HalfWidthPunctuation(WPARAM key, bool shifted) {
  if (shifted) {
    switch (key) {
      case L'0':
        return L")";
      case L'1':
        return L"!";
      case L'2':
        return L"@";
      case L'3':
        return L"#";
      case L'4':
        return L"$";
      case L'5':
        return L"%";
      case L'6':
        return L"^";
      case L'7':
        return L"&";
      case L'8':
        return L"*";
      case L'9':
        return L"(";
      case VK_OEM_1:
        return L":";
      case VK_OEM_2:
        return L"?";
      case VK_OEM_3:
        return L"~";
      case VK_OEM_4:
        return L"{";
      case VK_OEM_5:
        return L"|";
      case VK_OEM_6:
        return L"}";
      case VK_OEM_7:
        return L"\"";
      case VK_OEM_COMMA:
        return L"<";
      case VK_OEM_MINUS:
        return L"_";
      case VK_OEM_PERIOD:
        return L">";
      case VK_OEM_PLUS:
        return L"+";
      default:
        return {};
    }
  }

  switch (key) {
    case VK_OEM_1:
      return L";";
    case VK_OEM_2:
      return L"/";
    case VK_OEM_3:
      return L"`";
    case VK_OEM_4:
      return L"[";
    case VK_OEM_5:
      return L"\\";
    case VK_OEM_6:
      return L"]";
    case VK_OEM_7:
      return L"'";
    case VK_OEM_COMMA:
      return L",";
    case VK_OEM_MINUS:
      return L"-";
    case VK_OEM_PERIOD:
      return L".";
    case VK_OEM_PLUS:
      return L"=";
    default:
      return {};
  }
}

std::wstring FullWidthPunctuation(WPARAM key, bool shifted, bool* opening_quote) {
  if (shifted) {
    switch (key) {
      case L'0':
        return L"）";
      case L'1':
        return L"！";
      case L'2':
        return L"＠";
      case L'3':
        return L"＃";
      case L'4':
        return L"￥";
      case L'5':
        return L"％";
      case L'6':
        return L"……";
      case L'7':
        return L"＆";
      case L'8':
        return L"＊";
      case L'9':
        return L"（";
      case VK_OEM_1:
        return L"：";
      case VK_OEM_2:
        return L"？";
      case VK_OEM_3:
        return L"～";
      case VK_OEM_4:
        return L"｛";
      case VK_OEM_5:
        return L"｜";
      case VK_OEM_6:
        return L"｝";
      case VK_OEM_7: {
        const bool use_opening = opening_quote == nullptr || *opening_quote;
        if (opening_quote != nullptr) {
          *opening_quote = !*opening_quote;
        }
        return use_opening ? L"“" : L"”";
      }
      case VK_OEM_COMMA:
        return L"《";
      case VK_OEM_MINUS:
        return L"——";
      case VK_OEM_PERIOD:
        return L"》";
      case VK_OEM_PLUS:
        return L"＋";
      default:
        return {};
    }
  }

  switch (key) {
    case VK_OEM_1:
      return L"；";
    case VK_OEM_2:
      return L"／";
    case VK_OEM_3:
      return L"·";
    case VK_OEM_4:
      return L"【";
    case VK_OEM_5:
      return L"、";
    case VK_OEM_6:
      return L"】";
    case VK_OEM_7: {
      const bool use_opening = opening_quote == nullptr || *opening_quote;
      if (opening_quote != nullptr) {
        *opening_quote = !*opening_quote;
      }
      return use_opening ? L"‘" : L"’";
    }
    case VK_OEM_COMMA:
      return L"，";
    case VK_OEM_MINUS:
      return L"－";
    case VK_OEM_PERIOD:
      return L"。";
    case VK_OEM_PLUS:
      return L"＝";
    default:
      return {};
  }
}

std::wstring Punctuation(WPARAM key, bool shifted, core::PunctuationStyle style,
                         bool* opening_quote) {
  if (style == core::PunctuationStyle::kHalfWidth) {
    return HalfWidthPunctuation(key, shifted);
  }
  return FullWidthPunctuation(key, shifted, opening_quote);
}

std::wstring PairedPunctuation(WPARAM key, bool shifted, core::PunctuationStyle style) {
  if (style == core::PunctuationStyle::kHalfWidth) {
    if (shifted) {
      switch (key) {
        case L'9':
          return L"()";
        case VK_OEM_4:
          return L"{}";
        case VK_OEM_7:
          return L"\"\"";
        case VK_OEM_COMMA:
          return L"<>";
        default:
          return {};
      }
    }
    switch (key) {
      case VK_OEM_4:
        return L"[]";
      case VK_OEM_7:
        return L"''";
      default:
        return {};
    }
  }

  if (shifted) {
    switch (key) {
      case L'9':
        return L"（）";
      case VK_OEM_4:
        return L"｛｝";
      case VK_OEM_7:
        return L"“”";
      case VK_OEM_COMMA:
        return L"《》";
      default:
        return {};
    }
  }
  switch (key) {
    case VK_OEM_4:
      return L"【】";
    case VK_OEM_7:
      return L"‘’";
    default:
      return {};
  }
}

void AppendStartupFallbackKey(const detail::StartupKey& key,
                              core::PunctuationStyle punctuation_style,
                              bool* opening_quote, std::wstring* text) {
  const WPARAM value = static_cast<WPARAM>(key.key);
  if (IsLetterKey(value)) {
    const wchar_t letter = static_cast<wchar_t>(value);
    text->push_back(key.shifted ? letter : static_cast<wchar_t>(letter - L'A' + L'a'));
  } else if (value == VK_OEM_7 && !key.shifted) {
    text->push_back(L'\'');
  } else if (value == VK_BACK) {
    if (!text->empty()) {
      text->pop_back();
    }
  } else if (value == VK_ESCAPE) {
    text->clear();
  } else if (value == VK_SPACE) {
    text->push_back(L' ');
  } else if (value == VK_RETURN) {
    text->push_back(L'\n');
  } else if (value >= L'0' && value <= L'9') {
    text->push_back(static_cast<wchar_t>(value));
  } else {
    text->append(Punctuation(value, key.shifted, punctuation_style, opening_quote));
  }
}

std::optional<std::filesystem::path> BrokerPath() {
  std::wstring module_path(32768, L'\0');
  const DWORD length = GetModuleFileNameW(ModuleInstance(), module_path.data(),
                                          static_cast<DWORD>(module_path.size()));
  if (length == 0 || static_cast<std::size_t>(length) >= module_path.size()) {
    return std::nullopt;
  }
  module_path.resize(length);
  return std::filesystem::path(module_path).parent_path() / L"ZiliuBroker.exe";
}

std::optional<std::filesystem::path> SettingsExecutablePath() {
  const auto broker_path = BrokerPath();
  if (!broker_path.has_value()) {
    return std::nullopt;
  }
  return broker_path->parent_path() / L"ZiliuSettings.exe";
}

}  // namespace

TextService::TextService() : state_(std::make_unique<TextServiceState>()) {
  AddModuleReference();
}

TextService::~TextService() {
  Deactivate();
  ReleaseModuleReference();
}

STDMETHODIMP TextService::QueryInterface(REFIID interface_id, void** object) {
  if (object == nullptr) {
    return E_INVALIDARG;
  }
  *object = nullptr;

  if (IsEqualIID(interface_id, IID_IUnknown) ||
      IsEqualIID(interface_id, IID_ITfTextInputProcessor)) {
    *object = static_cast<ITfTextInputProcessor*>(this);
  } else if (IsEqualIID(interface_id, IID_ITfTextInputProcessorEx)) {
    *object = static_cast<ITfTextInputProcessorEx*>(this);
  } else if (IsEqualIID(interface_id, IID_ITfKeyEventSink)) {
    *object = static_cast<ITfKeyEventSink*>(this);
  } else if (IsEqualIID(interface_id, IID_ITfCompartmentEventSink)) {
    *object = static_cast<ITfCompartmentEventSink*>(this);
  } else if (IsEqualIID(interface_id, IID_ITfThreadFocusSink)) {
    *object = static_cast<ITfThreadFocusSink*>(this);
  }

  if (*object == nullptr) {
    return E_NOINTERFACE;
  }
  AddRef();
  return S_OK;
}

STDMETHODIMP_(ULONG) TextService::AddRef() { return ++reference_count_; }

STDMETHODIMP_(ULONG) TextService::Release() {
  const ULONG count = --reference_count_;
  if (count == 0) {
    delete this;
  }
  return count;
}

STDMETHODIMP TextService::Activate(ITfThreadMgr* thread_manager, TfClientId client_id) {
  return ActivateEx(thread_manager, client_id, 0);
}

STDMETHODIMP TextService::ActivateEx(ITfThreadMgr* thread_manager, TfClientId client_id,
                                     DWORD flags) {
  if (thread_manager == nullptr) {
    return E_INVALIDARG;
  }
  if (thread_manager_ != nullptr) {
    return E_UNEXPECTED;
  }

  thread_manager_ = thread_manager;
  thread_manager_->AddRef();
  client_id_ = client_id;
  activation_flags_ = flags;

  // Secure activation must not start the broker, load user settings, or expose
  // settings UI. This alpha deliberately leaves secure input to the application.
  if ((flags & TF_TMAE_SECUREMODE) != 0) {
    return S_OK;
  }

  ITfKeystrokeMgr* keystroke_manager = nullptr;
  const HRESULT query_result =
      thread_manager_->QueryInterface(IID_PPV_ARGS(&keystroke_manager));
  if (FAILED(query_result)) {
    Deactivate();
    return query_result;
  }

  const HRESULT advise_result = keystroke_manager->AdviseKeyEventSink(client_id_, this, TRUE);
  keystroke_manager->Release();
  if (FAILED(advise_result)) {
    Deactivate();
    return advise_result;
  }

  const HRESULT input_mode_sink_result = AdviseInputModeSinks();
  if (FAILED(input_mode_sink_result)) {
    Deactivate();
    return input_mode_sink_result;
  }

  state_->candidate_window.SetThemeResourceLoader(
      [this](std::string_view theme_id,
             std::string_view resource) -> std::optional<std::vector<std::byte>> {
        const std::size_t limit = resource.empty() ? core::kMaximumThemeManifestBytes
                                                    : core::kMaximumThemeAssetBytes;
        if (theme_id.empty() ||
            (!resource.empty() && !core::IsSafeThemeAssetPath(resource))) {
          return std::nullopt;
        }
        std::vector<std::byte> contents;
        for (;;) {
          core::ipc::Request request{state_->request_id++, 0,
                                     core::ipc::Command::kGetThemeResource,
                                     static_cast<std::uint32_t>(contents.size())};
          request.theme_id = theme_id;
          request.resource = resource;
          const auto response = state_->client.Exchange(request);
          if (!response.has_value() || response->status != core::ipc::Status::kOk ||
              response->theme_chunk.size() > limit - contents.size()) {
            return std::nullopt;
          }
          contents.insert(contents.end(), response->theme_chunk.begin(),
                          response->theme_chunk.end());
          if (response->theme_chunk.size() < core::ipc::kMaximumThemeChunkBytes) {
            return contents;
          }
        }
      });

  ITfLangBarItemMgr* language_bar_manager = nullptr;
  if (SUCCEEDED(thread_manager_->QueryInterface(IID_PPV_ARGS(&language_bar_manager)))) {
    const auto settings_path = SettingsExecutablePath();
    auto* language_bar_button = new (std::nothrow) LanguageBarButton(
        settings_path.has_value() ? settings_path->native() : std::wstring{}, [this]() {
          BOOL eaten = FALSE;
          return ToggleInputMode(nullptr, &eaten, false);
        }, [this](LONG x, LONG y) {
          StartBroker();
          core::ipc::Request request{state_->request_id++, 0,
                                     core::ipc::Command::kOpenQuickMenu, 0};
          request.point_x = x;
          request.point_y = y;
          const auto response = state_->client.Exchange(request);
          return response.has_value() && response->status == core::ipc::Status::kOk
                     ? S_OK : E_FAIL;
        }, [this](std::uint32_t action) {
          StartBroker();
          const core::ipc::Request request{state_->request_id++, 0,
                                           core::ipc::Command::kRunMenuAction, action};
          const auto response = state_->client.Exchange(request);
          return response.has_value() && response->status == core::ipc::Status::kOk
                     ? S_OK : E_FAIL;
        });
    if (language_bar_button != nullptr &&
        SUCCEEDED(language_bar_manager->AddItem(language_bar_button))) {
      state_->language_bar_manager = language_bar_manager;
      state_->language_bar_button = language_bar_button;
      state_->language_bar_button->SetChineseMode(state_->chinese_mode);
      static_cast<void>(state_->language_bar_button->Show(TRUE));
      state_->candidate_window.SetQuickMenuAction([this](POINT anchor) {
        if (state_->language_bar_button != nullptr) {
          static_cast<void>(
              state_->language_bar_button->ShowQuickMenu(anchor.x, anchor.y));
          return;
        }
        const auto settings_path = SettingsExecutablePath();
        if (!settings_path.has_value()) {
          return;
        }
        const std::wstring arguments =
            L"--quick-menu --x " + std::to_wstring(anchor.x) +
            L" --y " + std::to_wstring(anchor.y);
        static_cast<void>(ShellExecuteW(nullptr, L"open", settings_path->c_str(),
                                        arguments.c_str(),
                                        settings_path->parent_path().c_str(),
                                        SW_SHOWNORMAL));
      });
    } else {
      if (language_bar_button != nullptr) {
        language_bar_button->Release();
      }
      language_bar_manager->Release();
    }
  }

  RefreshSettings(true);
  state_->chinese_mode =
      state_->settings.default_input_mode == core::DefaultInputMode::kChinese;
  PublishInputMode();
  StartBroker();
  return S_OK;
}

STDMETHODIMP TextService::Deactivate() {
  if (thread_manager_ == nullptr) {
    return S_OK;
  }

  if (state_->session_id != 0) {
    const core::ipc::Request close_request{state_->request_id++, state_->session_id,
                                           core::ipc::Command::kCloseSession, 0};
    static_cast<void>(state_->client.Exchange(close_request));
  }
  ResetRuntimeState();
  UnadviseInputModeSinks();

  if (state_->language_bar_manager != nullptr && state_->language_bar_button != nullptr) {
    static_cast<void>(state_->language_bar_manager->RemoveItem(state_->language_bar_button));
    state_->language_bar_button->Release();
    state_->language_bar_button = nullptr;
    state_->language_bar_manager->Release();
    state_->language_bar_manager = nullptr;
  }

  ITfKeystrokeMgr* keystroke_manager = nullptr;
  if (SUCCEEDED(thread_manager_->QueryInterface(IID_PPV_ARGS(&keystroke_manager)))) {
    keystroke_manager->UnadviseKeyEventSink(client_id_);
    keystroke_manager->Release();
  }

  thread_manager_->Release();
  thread_manager_ = nullptr;
  client_id_ = TF_CLIENTID_NULL;
  activation_flags_ = 0;
  return S_OK;
}

void TextService::StartBroker() {
  if (state_->broker_started) {
    return;
  }
  const auto broker_path = BrokerPath();
  std::error_code file_error;
  if (!broker_path.has_value() ||
      !std::filesystem::is_regular_file(*broker_path, file_error) || file_error) {
    return;
  }

  STARTUPINFOW startup_info{};
  startup_info.cb = sizeof(startup_info);
  PROCESS_INFORMATION process_info{};
  std::wstring command_line = L"\"" + broker_path->native() + L"\"";
  std::vector<wchar_t> mutable_command(command_line.begin(), command_line.end());
  mutable_command.push_back(L'\0');
  if (CreateProcessW(broker_path->c_str(), mutable_command.data(), nullptr, nullptr, FALSE,
                     CREATE_NO_WINDOW | DETACHED_PROCESS, nullptr, broker_path->parent_path().c_str(),
                     &startup_info, &process_info)) {
    state_->broker_started = true;
    CloseHandle(process_info.hThread);
    CloseHandle(process_info.hProcess);
  }
}

bool TextService::EnsureSession() {
  if ((activation_flags_ & TF_TMAE_SECUREMODE) != 0) {
    return false;
  }
  if (state_->session_id != 0) {
    return true;
  }
  StartBroker();
  // Broker startup performs Rime deployment before publishing the pipe. A key
  // callback must never wait for that work; readiness is polled by the startup
  // timer and a normal bounded exchange is used only after the pipe exists.
  if (!state_->client.IsServerAvailable()) {
    return false;
  }
  const std::uint32_t restricted =
      state_->key_privacy == detail::InputPrivacy::kRestricted ? 1U : 0U;
  const core::ipc::Request request{state_->request_id++, 0,
                                   core::ipc::Command::kCreateSession, restricted};
  const auto response = state_->client.Exchange(request);
  if (!response.has_value() || response->status != core::ipc::Status::kOk ||
      response->session_id == 0) {
    return false;
  }
  state_->session_id = response->session_id;
  state_->snapshot = response->snapshot;
  const core::ipc::Request option_request{
      state_->request_id++, state_->session_id, core::ipc::Command::kSetTraditional,
      state_->settings.character_set == core::CharacterSet::kTraditional ? 1U : 0U};
  static_cast<void>(state_->client.Exchange(option_request));
  const core::ipc::Request page_size_request{
      state_->request_id++, state_->session_id, core::ipc::Command::kSetCandidatePageSize,
      static_cast<std::uint32_t>(state_->settings.candidate_count)};
  static_cast<void>(state_->client.Exchange(page_size_request));
  const core::ipc::Request page_window_request{
      state_->request_id++, state_->session_id,
      core::ipc::Command::kSetCandidateWindowPageCount,
      state_->settings.candidate_page_mode == core::CandidatePageMode::kMultiLine
          ? static_cast<std::uint32_t>(core::kCandidateWindowPageCount)
          : 1U};
  static_cast<void>(state_->client.Exchange(page_window_request));
  const core::ipc::Request candidate_filter_request{
      state_->request_id++, state_->session_id,
      core::ipc::Command::kSetChineseCandidatesOnly,
      state_->settings.chinese_candidates_only ? 1U : 0U};
  static_cast<void>(state_->client.Exchange(candidate_filter_request));
  return true;
}

bool TextService::EnsureStartupWindow() {
  if (state_->startup_window != nullptr) {
    return true;
  }
  WNDCLASSW window_class{};
  window_class.lpfnWndProc = StartupWindowProcedure;
  window_class.hInstance = ModuleInstance();
  window_class.lpszClassName = kStartupWindowClass;
  if (RegisterClassW(&window_class) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
    return false;
  }
  state_->startup_window =
      CreateWindowExW(0, kStartupWindowClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE,
                      nullptr, ModuleInstance(), this);
  return state_->startup_window != nullptr;
}

bool TextService::IsFocusedContext(ITfContext* context) const {
  if (thread_manager_ == nullptr || context == nullptr) {
    return false;
  }
  Microsoft::WRL::ComPtr<ITfDocumentMgr> document;
  if (FAILED(thread_manager_->GetFocus(document.GetAddressOf())) || document == nullptr) {
    return false;
  }
  Microsoft::WRL::ComPtr<ITfContext> focused_context;
  return SUCCEEDED(document->GetTop(focused_context.GetAddressOf())) &&
         focused_context.Get() == context;
}

bool TextService::CaptureStartupTarget(TfEditCookie cookie, ITfContext* context,
                                       bool capture_windows) {
  TF_SELECTION selection{};
  ULONG fetched = 0;
  if (context == nullptr || FAILED(context->GetSelection(
                                cookie, TF_DEFAULT_SELECTION, 1, &selection, &fetched)) ||
      fetched != 1 || selection.range == nullptr) {
    return false;
  }
  state_->startup_selection.Attach(selection.range);
  if (capture_windows) {
    state_->startup_focus = GetFocus();
    state_->startup_foreground = GetForegroundWindow();
  }
  return true;
}

bool TextService::ValidateStartupTarget(TfEditCookie cookie, ITfContext* context) const {
  if (context == nullptr || state_->startup_selection == nullptr ||
      GetFocus() != state_->startup_focus ||
      GetForegroundWindow() != state_->startup_foreground) {
    return false;
  }
  TF_SELECTION selection{};
  ULONG fetched = 0;
  if (FAILED(context->GetSelection(cookie, TF_DEFAULT_SELECTION, 1, &selection, &fetched)) ||
      fetched != 1 || selection.range == nullptr) {
    return false;
  }
  Microsoft::WRL::ComPtr<ITfRange> current;
  current.Attach(selection.range);
  BOOL same_start = FALSE;
  BOOL same_end = FALSE;
  return SUCCEEDED(state_->startup_selection->IsEqualStart(
             cookie, current.Get(), TF_ANCHOR_START, &same_start)) &&
         SUCCEEDED(state_->startup_selection->IsEqualEnd(
             cookie, current.Get(), TF_ANCHOR_END, &same_end)) &&
         same_start != FALSE && same_end != FALSE;
}

LRESULT CALLBACK TextService::StartupWindowProcedure(HWND window, UINT message,
                                                     WPARAM wparam, LPARAM lparam) {
  TextService* service = nullptr;
  if (message == WM_NCCREATE) {
    const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
    service = static_cast<TextService*>(create->lpCreateParams);
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(service));
  } else {
    service = reinterpret_cast<TextService*>(GetWindowLongPtrW(window, GWLP_USERDATA));
  }
  if (service != nullptr && message == WM_TIMER && wparam == kStartupTimer) {
    service->OnStartupTimer();
    return 0;
  }
  if (message == WM_NCDESTROY) {
    SetWindowLongPtrW(window, GWLP_USERDATA, 0);
  }
  return DefWindowProcW(window, message, wparam, lparam);
}

bool TextService::ScheduleStartupReplay() {
  if (state_->startup_timer_active || state_->startup_replay_scheduled ||
      state_->startup_keys.empty()) {
    return true;
  }
  if (!EnsureStartupWindow() ||
      SetTimer(state_->startup_window, kStartupTimer, kStartupPollMilliseconds, nullptr) == 0) {
    return false;
  }
  state_->startup_timer_active = true;
  if (state_->startup_started_time == 0) {
    state_->startup_started_time = GetTickCount64();
  }
  return true;
}

void TextService::CancelStartupReplay(bool destroy_window) {
  if (state_->startup_timer_active && state_->startup_window != nullptr) {
    KillTimer(state_->startup_window, kStartupTimer);
  }
  state_->startup_timer_active = false;
  state_->startup_replay_scheduled = false;
  state_->startup_started_time = 0;
  state_->startup_context.Reset();
  state_->startup_selection.Reset();
  state_->startup_focus = nullptr;
  state_->startup_foreground = nullptr;
  state_->startup_keys.Cancel();
  if (destroy_window && state_->startup_window != nullptr) {
    DestroyWindow(state_->startup_window);
    state_->startup_window = nullptr;
    // The class owns a DLL window procedure. Remove it when the final window in
    // this process is gone so the module can never unload with a stale callback.
    static_cast<void>(UnregisterClassW(kStartupWindowClass, ModuleInstance()));
  }
}

void TextService::OnStartupTimer() {
  if (state_->startup_keys.empty() || state_->startup_context == nullptr ||
      state_->startup_context.Get() != state_->key_context.Get() ||
      state_->key_privacy == detail::InputPrivacy::kBlocked ||
      !IsFocusedContext(state_->startup_context.Get()) ||
      GetFocus() != state_->startup_focus ||
      GetForegroundWindow() != state_->startup_foreground) {
    CancelStartupReplay(false);
    return;
  }
  const ULONGLONG now = GetTickCount64();
  if (state_->startup_started_time != 0 &&
      now - state_->startup_started_time >= kStartupMaximumMilliseconds) {
    // Preserve accepted typing without waiting indefinitely for the broker. The
    // fallback is a fresh, generation-checked TSF edit in the same context; it
    // never synthesizes keys or crosses a privacy/focus transition.
    state_->broker_started = false;
    RequestStartupEdit(true);
    return;
  }
  if (!state_->client.IsServerAvailable()) {
    return;
  }

  RequestStartupEdit(false);
}

void TextService::RequestStartupEdit(bool fallback) {
  if (state_->startup_window != nullptr && state_->startup_timer_active) {
    KillTimer(state_->startup_window, kStartupTimer);
  }
  state_->startup_timer_active = false;
  const std::uint64_t generation = state_->startup_keys.generation();
  auto* edit = new (std::nothrow) CompositionEditSession(
      this, state_->startup_context.Get(), generation,
      fallback ? CompositionEditSession::StartupMode::kFallback
               : CompositionEditSession::StartupMode::kReplay);
  if (edit == nullptr) {
    CancelStartupReplay(false);
    return;
  }
  HRESULT edit_result = E_FAIL;
  const HRESULT requested = state_->startup_context->RequestEditSession(
      client_id_, edit, TF_ES_ASYNC | TF_ES_READWRITE, &edit_result);
  edit->Release();
  if (FAILED(requested)) {
    CancelStartupReplay(false);
    return;
  }
  state_->startup_replay_scheduled = true;
}

void TextService::RefreshSettings(bool force) {
  const auto path = SettingsPath();
  std::optional<std::string> contents;
  std::filesystem::file_time_type write_time{};
  std::error_code time_error;
  if (path.has_value()) {
    write_time = std::filesystem::last_write_time(*path, time_error);
    if (!time_error) {
      if (!force && state_->settings_file_known && write_time == state_->settings_write_time) {
        return;
      }
      contents = ReadSettingsFile(*path);
    }
  }
  const bool remote = !contents.has_value();
  if (!contents.has_value()) {
    // AppContainer hosts cannot read the desktop user's settings file. Reuse
    // the authenticated pipe; do not weaken the profile directory's ACL.
    const ULONGLONG now = GetTickCount64();
    if (!force && now - state_->remote_settings_probe_time < 500) {
      return;
    }
    state_->remote_settings_probe_time = now;
    StartBroker();
    if (!state_->client.IsServerAvailable()) {
      return;
    }
    const core::ipc::Request request{state_->request_id++, 0, core::ipc::Command::kGetSettings, 0};
    const auto response = state_->client.Exchange(request);
    if (!response.has_value() || response->status != core::ipc::Status::kOk) {
      return;  // Keep the last good preferences on temporary broker/read failures.
    }
    contents = response->settings_text;
  }
  const core::Settings updated_settings = core::ParseSettings(*contents);
  state_->settings_write_time = write_time;
  state_->settings_file_known = !remote;
  if (updated_settings == state_->settings) {
    return;
  }
  const core::CharacterSet previous_character_set = state_->settings.character_set;
  const std::size_t previous_candidate_count = state_->settings.candidate_count;
  const core::CandidatePageMode previous_page_mode =
      state_->settings.candidate_page_mode;
  const bool previous_chinese_candidates_only =
      state_->settings.chinese_candidates_only;
  state_->settings = updated_settings;
  state_->candidate_page_offset = 0;

  if (state_->session_id != 0 && previous_character_set != state_->settings.character_set) {
    const core::ipc::Request request{
        state_->request_id++, state_->session_id, core::ipc::Command::kSetTraditional,
        state_->settings.character_set == core::CharacterSet::kTraditional ? 1U : 0U};
    static_cast<void>(state_->client.Exchange(request));
  }
  if (state_->session_id != 0 && previous_candidate_count != state_->settings.candidate_count) {
    const core::ipc::Request request{
        state_->request_id++, state_->session_id, core::ipc::Command::kSetCandidatePageSize,
        static_cast<std::uint32_t>(state_->settings.candidate_count)};
    static_cast<void>(state_->client.Exchange(request));
  }
  if (state_->session_id != 0 &&
      previous_page_mode != state_->settings.candidate_page_mode) {
    const core::ipc::Request request{
        state_->request_id++, state_->session_id,
        core::ipc::Command::kSetCandidateWindowPageCount,
        state_->settings.candidate_page_mode == core::CandidatePageMode::kMultiLine
            ? static_cast<std::uint32_t>(core::kCandidateWindowPageCount)
            : 1U};
    static_cast<void>(state_->client.Exchange(request));
  }
  if (state_->session_id != 0 &&
      previous_chinese_candidates_only != state_->settings.chinese_candidates_only) {
    const core::ipc::Request request{
        state_->request_id++, state_->session_id,
        core::ipc::Command::kSetChineseCandidatesOnly,
        state_->settings.chinese_candidates_only ? 1U : 0U};
    static_cast<void>(state_->client.Exchange(request));
  }
}

HRESULT TextService::AdviseInputModeSinks() {
  ITfCompartmentMgr* compartment_manager = nullptr;
  HRESULT result = thread_manager_->QueryInterface(IID_PPV_ARGS(&compartment_manager));
  if (FAILED(result)) {
    return result;
  }

  const auto advise_compartment =
      [this, compartment_manager](REFGUID guid, ITfSource** source, DWORD* cookie) {
        ITfCompartment* compartment = nullptr;
        HRESULT advise_result = compartment_manager->GetCompartment(guid, &compartment);
        if (SUCCEEDED(advise_result)) {
          advise_result = compartment->QueryInterface(IID_PPV_ARGS(source));
        }
        if (SUCCEEDED(advise_result)) {
          advise_result = (*source)->AdviseSink(
              IID_ITfCompartmentEventSink, static_cast<ITfCompartmentEventSink*>(this), cookie);
        }
        if (compartment != nullptr) {
          compartment->Release();
        }
        if (FAILED(advise_result) && *source != nullptr) {
          (*source)->Release();
          *source = nullptr;
          *cookie = TF_INVALID_COOKIE;
        }
        return advise_result;
      };

  result = advise_compartment(GUID_COMPARTMENT_KEYBOARD_OPENCLOSE, &open_close_source_,
                              &open_close_cookie_);
  if (SUCCEEDED(result)) {
    result = advise_compartment(GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION,
                                &conversion_source_, &conversion_cookie_);
  }
  compartment_manager->Release();

  if (SUCCEEDED(result)) {
    result = thread_manager_->QueryInterface(IID_PPV_ARGS(&thread_focus_source_));
  }
  if (SUCCEEDED(result)) {
    result = thread_focus_source_->AdviseSink(
        IID_ITfThreadFocusSink, static_cast<ITfThreadFocusSink*>(this), &thread_focus_cookie_);
  }
  if (FAILED(result)) {
    UnadviseInputModeSinks();
  }
  return result;
}

void TextService::UnadviseInputModeSinks() {
  const auto unadvise = [](ITfSource** source, DWORD* cookie) {
    if (*source != nullptr) {
      if (*cookie != TF_INVALID_COOKIE) {
        static_cast<void>((*source)->UnadviseSink(*cookie));
      }
      (*source)->Release();
      *source = nullptr;
    }
    *cookie = TF_INVALID_COOKIE;
  };
  unadvise(&thread_focus_source_, &thread_focus_cookie_);
  unadvise(&conversion_source_, &conversion_cookie_);
  unadvise(&open_close_source_, &open_close_cookie_);
}

bool TextService::ReadPublishedInputMode(bool* chinese_mode) const {
  if (chinese_mode == nullptr || thread_manager_ == nullptr) {
    return false;
  }

  ITfCompartmentMgr* compartment_manager = nullptr;
  if (FAILED(thread_manager_->QueryInterface(IID_PPV_ARGS(&compartment_manager)))) {
    return false;
  }

  bool found = false;
  ITfCompartment* keyboard_open = nullptr;
  if (SUCCEEDED(compartment_manager->GetCompartment(GUID_COMPARTMENT_KEYBOARD_OPENCLOSE,
                                                     &keyboard_open))) {
    VARIANT value{};
    if (SUCCEEDED(keyboard_open->GetValue(&value)) && value.vt == VT_I4) {
      *chinese_mode = value.lVal != 0;
      found = true;
    }
    VariantClear(&value);
    keyboard_open->Release();
  }

  if (!found) {
    ITfCompartment* conversion = nullptr;
    if (SUCCEEDED(compartment_manager->GetCompartment(
            GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION, &conversion))) {
      VARIANT value{};
      if (SUCCEEDED(conversion->GetValue(&value)) && value.vt == VT_I4) {
        *chinese_mode = (value.lVal & TF_CONVERSIONMODE_NATIVE) != 0;
        found = true;
      }
      VariantClear(&value);
      conversion->Release();
    }
  }
  compartment_manager->Release();
  return found;
}

void TextService::SynchronizeInputMode() {
  bool chinese_mode = state_->chinese_mode;
  if (!ReadPublishedInputMode(&chinese_mode) || chinese_mode == state_->chinese_mode) {
    return;
  }
  CancelStartupReplay(false);
  state_->chinese_mode = chinese_mode;
  state_->snapshot = {};
  state_->pending_response = {};
  state_->candidate_page_offset = 0;
  state_->pending_caret_back = 0;
  state_->candidate_window.Hide();
  if (state_->language_bar_button != nullptr) {
    state_->language_bar_button->SetChineseMode(chinese_mode);
  }
}

void TextService::PublishInputMode() {
  if (thread_manager_ == nullptr || client_id_ == TF_CLIENTID_NULL) {
    return;
  }

  ITfCompartmentMgr* compartment_manager = nullptr;
  if (FAILED(thread_manager_->QueryInterface(IID_PPV_ARGS(&compartment_manager)))) {
    return;
  }

  state_->publishing_input_mode = true;

  ITfCompartment* input_mode = nullptr;
  if (SUCCEEDED(compartment_manager->GetCompartment(
          GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION, &input_mode))) {
    VARIANT value{};
    value.vt = VT_I4;
    value.lVal = state_->chinese_mode ? TF_CONVERSIONMODE_NATIVE
                                      : TF_CONVERSIONMODE_ALPHANUMERIC;
    static_cast<void>(input_mode->SetValue(client_id_, &value));
    input_mode->Release();
  }

  ITfCompartment* keyboard_open = nullptr;
  if (SUCCEEDED(compartment_manager->GetCompartment(GUID_COMPARTMENT_KEYBOARD_OPENCLOSE,
                                                     &keyboard_open))) {
    VARIANT value{};
    value.vt = VT_I4;
    // Windows derives the built-in 中/英 mode indicator from this open/close state.
    value.lVal = state_->chinese_mode ? 1 : 0;
    static_cast<void>(keyboard_open->SetValue(client_id_, &value));
    keyboard_open->Release();
  }
  compartment_manager->Release();
  state_->publishing_input_mode = false;
}

void TextService::ResetRuntimeState() {
  ClearCommittedPairCaret();
  CancelStartupReplay(true);
  state_->key_context.Reset();
  state_->key_privacy = detail::InputPrivacy::kBlocked;
  state_->session_id = 0;
  state_->snapshot = {};
  state_->pending_response = {};
  state_->candidate_page_offset = 0;
  state_->pending_caret_back = 0;
  state_->switch_key_down = false;
  state_->switch_key_used = false;
  state_->candidate_window.Hide();
  state_->broker_started = false;
}

void TextService::AbandonSession(ITfContext* context) {
  static_cast<void>(context);
  CancelStartupReplay(false);
  if (state_->session_id != 0) {
    const core::ipc::Request close_request{state_->request_id++, state_->session_id,
                                           core::ipc::Command::kCloseSession, 0};
    static_cast<void>(state_->client.Exchange(close_request));
  }
  state_->session_id = 0;
  state_->key_context.Reset();
  state_->key_privacy = detail::InputPrivacy::kBlocked;
  state_->snapshot = {};
  state_->pending_response = {};
  state_->candidate_page_offset = 0;
  state_->pending_caret_back = 0;
  state_->candidate_window.Hide();
  state_->broker_started = false;
}

bool TextService::IsInputModeSwitchKey(WPARAM wparam) const {
  if (state_->settings.input_mode_switch_key == core::InputModeSwitchKey::kControl) {
    return wparam == VK_CONTROL || wparam == VK_LCONTROL || wparam == VK_RCONTROL;
  }
  return wparam == VK_SHIFT || wparam == VK_LSHIFT || wparam == VK_RSHIFT;
}

bool TextService::AllowKeyInContext(ITfContext* context, const TfEditCookie* cookie) {
  const detail::InputPrivacy privacy = cookie == nullptr
      ? detail::ClassifyInputContext(context, client_id_, activation_flags_)
      : detail::ContextBlocksInput(context, activation_flags_)
            ? detail::InputPrivacy::kBlocked
            : detail::ClassifyScope(context, *cookie);
  if (privacy != detail::InputPrivacy::kBlocked) {
    if (detail::ShouldResetInputSession(
            state_->key_privacy, privacy,
            state_->key_context != nullptr && state_->key_context.Get() != context)) {
      ClearCommittedPairCaret();
      state_->switch_key_down = false;
      state_->switch_key_used = false;
      AbandonSession(context);
    }
    state_->key_context = context;
    state_->key_privacy = privacy;
    return true;
  }
  // Drop old preedit without committing it. No new key or sensitive field text
  // is included in the close-session request, and the next safe field starts fresh.
  ClearCommittedPairCaret();
  state_->switch_key_down = false;
  state_->switch_key_used = false;
  AbandonSession(context);
  return false;
}

bool TextService::ShouldHandleKey(WPARAM wparam) const {
  if (IsInputModeSwitchKey(wparam)) {
    return true;
  }
  if (!state_->chinese_mode || HasAltModifier() || HasControlModifier()) {
    return false;
  }
  if (IsLetterKey(wparam)) {
    return true;
  }
  const bool startup_composition = state_->startup_keys.HasCompositionIntent();
  if (wparam == VK_BACK || wparam == VK_ESCAPE) {
    return !state_->snapshot.preedit.empty() || !state_->startup_keys.empty();
  }
  if (wparam == VK_SPACE) {
    return !state_->snapshot.candidates.empty() || !state_->startup_keys.empty();
  }
  if (wparam == VK_RETURN) {
    return !state_->snapshot.preedit.empty() || !state_->startup_keys.empty();
  }
  const bool shifted = HasShiftModifier() ||
                       (state_->settings.input_mode_switch_key ==
                            core::InputModeSwitchKey::kShift &&
                        state_->switch_key_down);
  if (!shifted && !state_->startup_keys.empty() && wparam >= L'1' && wparam <= L'9') {
    return true;
  }
  if (!shifted && wparam >= L'1' && wparam <= L'9') {
    const auto slice = core::MakeCandidatePageSlice(
        state_->snapshot.candidates.size(), state_->settings.candidate_count,
        state_->candidate_page_offset);
    const auto index = static_cast<std::size_t>(wparam - L'1');
    return index < slice.count;
  }
  if (!shifted && (!state_->snapshot.preedit.empty() || startup_composition) &&
      (IsPageKey(wparam, state_->settings.page_key_set, false) ||
       IsPageKey(wparam, state_->settings.page_key_set, true))) {
    return true;
  }
  if (!shifted && wparam == VK_OEM_7 && !state_->snapshot.preedit.empty()) {
    return true;
  }
  return !Punctuation(wparam, shifted, state_->settings.punctuation_style, nullptr).empty();
}

void TextService::ShowCandidateWindow() {
  if (state_->snapshot.empty()) {
    state_->candidate_window.Hide();
  } else if (state_->candidate_window.Create(state_->candidate_owner)) {
    state_->candidate_page_offset =
        core::MakeCandidatePageSlice(
            state_->snapshot.candidates.size(), state_->settings.candidate_count,
            state_->snapshot.highlighted_index)
            .offset;
    state_->candidate_window.Show(state_->snapshot, state_->candidate_anchor, state_->settings,
                                  state_->candidate_page_offset);
  }
}

STDMETHODIMP TextService::OnSetFocus(BOOL foreground) {
  if (foreground) {
    RefreshSettings(true);
    // Do not restore a candidate surface before the new context's privacy gate.
    state_->candidate_window.Hide();
    PublishInputMode();
  } else {
    AbandonSession(nullptr);
  }
  return S_OK;
}

STDMETHODIMP TextService::OnChange(REFGUID guid) {
  if (state_->publishing_input_mode) {
    return S_OK;
  }
  if (IsEqualGUID(guid, GUID_COMPARTMENT_KEYBOARD_OPENCLOSE) ||
      IsEqualGUID(guid, GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION)) {
    SynchronizeInputMode();
  }
  return S_OK;
}

STDMETHODIMP TextService::OnSetThreadFocus() {
  RefreshSettings(true);
  state_->candidate_window.Hide();
  PublishInputMode();
  return S_OK;
}

STDMETHODIMP TextService::OnKillThreadFocus() {
  ClearCommittedPairCaret();
  AbandonSession(nullptr);
  return S_OK;
}

STDMETHODIMP TextService::OnTestKeyDown(ITfContext* context, WPARAM wparam, LPARAM lparam,
                                      BOOL* eaten) {
  static_cast<void>(lparam);
  if (eaten == nullptr) {
    return E_INVALIDARG;
  }
  *eaten = FALSE;
  if (context == nullptr || (activation_flags_ & TF_TMAE_SECUREMODE) != 0) {
    return S_OK;
  }
  // A host can deny a synchronous read lock while asking whether we would
  // handle a key. Recheck the input scope under the actual key edit lock;
  // OnKeyDown returns FALSE for blocked contexts so the host receives the key.
  if (detail::ContextBlocksInput(context, activation_flags_)) {
    return S_OK;
  }
  RefreshSettings(false);
  // Never move the caret after another key has begun a new edit.
  ClearCommittedPairCaret();
  if (state_->switch_key_down && !IsInputModeSwitchKey(wparam)) {
    state_->switch_key_used = true;
  }
  if (!ShouldHandleKey(wparam)) {
    *eaten = FALSE;
    return S_OK;
  }
  // Only the actual key edit is allowed to create/use an engine session.
  *eaten = TRUE;
  return S_OK;
}

STDMETHODIMP TextService::OnKeyDown(ITfContext* context, WPARAM wparam, LPARAM lparam,
                                    BOOL* eaten) {
  static_cast<void>(lparam);
  return RequestKeyEdit(context, wparam, false, eaten);
}

HRESULT TextService::RequestKeyEdit(ITfContext* context, WPARAM key, bool key_up, BOOL* eaten) {
  if (context == nullptr || eaten == nullptr) {
    return E_INVALIDARG;
  }
  *eaten = FALSE;
  auto* edit = new (std::nothrow) CompositionEditSession(this, context, key, key_up);
  if (edit == nullptr) return S_OK;
  HRESULT result = E_FAIL;
  // Hold the same TSF write lock from privacy authorization through broker
  // processing and insertion. No key is forwarded before this callback runs.
  const HRESULT requested = context->RequestEditSession(
      client_id_, edit, TF_ES_SYNC | TF_ES_READWRITE, &result);
  if (requested == S_OK && result == S_OK) {
    *eaten = edit->eaten();
  } else {
    ClearCommittedPairCaret();
    state_->switch_key_down = false;
    state_->switch_key_used = false;
    AbandonSession(context);
  }
  edit->Release();
  // Policy refusal/lock failure is a key bypass, not a TSF callback failure.
  return S_OK;
}

HRESULT TextService::HandleKeyDown(TfEditCookie cookie, ITfContext* context,
                                  WPARAM wparam, BOOL* eaten) {
  if (!AllowKeyInContext(context, &cookie)) {
    return S_OK;
  }
  if (IsInputModeSwitchKey(wparam)) {
    state_->switch_key_down = true;
    state_->switch_key_used = false;
    *eaten = TRUE;
    return S_OK;
  }
  if (state_->switch_key_down) {
    state_->switch_key_used = true;
  }
  if (!ShouldHandleKey(wparam)) {
    return S_OK;
  }
  const bool shifted = HasShiftModifier() ||
                       (state_->settings.input_mode_switch_key ==
                            core::InputModeSwitchKey::kShift &&
                        state_->switch_key_down);
  if (wparam == VK_ESCAPE && !state_->startup_keys.empty()) {
    CancelStartupReplay(false);
    *eaten = TRUE;
    return S_OK;
  }

  const bool punctuation_without_preedit =
      state_->startup_keys.empty() && state_->snapshot.preedit.empty() &&
      (!PairedPunctuation(wparam, shifted, state_->settings.punctuation_style).empty() ||
       !Punctuation(wparam, shifted, state_->settings.punctuation_style, nullptr).empty());
  if (!state_->startup_keys.empty()) {
    QueueStartupKey(cookie, context, wparam, shifted, eaten);
    return S_OK;
  }
  if (state_->session_id == 0 && !punctuation_without_preedit && !EnsureSession()) {
    QueueStartupKey(cookie, context, wparam, shifted, eaten);
    return S_OK;
  }
  return ProcessKeyDown(context, wparam, shifted, eaten);
}

HRESULT TextService::ProcessKeyDown(ITfContext* context, WPARAM wparam, bool shifted,
                                    BOOL* eaten) {
  if (!shifted && !state_->snapshot.preedit.empty()) {
    if (IsPageKey(wparam, state_->settings.page_key_set, false)) {
      return HandleCandidatePage(context, false, eaten);
    }
    if (IsPageKey(wparam, state_->settings.page_key_set, true)) {
      return HandleCandidatePage(context, true, eaten);
    }
  }
  if (!shifted && wparam == VK_OEM_7 && !state_->snapshot.preedit.empty()) {
    return ApplyKeyResponse(context, wparam, eaten);
  }
  if (wparam == VK_RETURN && !state_->snapshot.preedit.empty()) {
    return CommitPendingInput(context, eaten);
  }
  const std::wstring paired_punctuation =
      state_->settings.auto_pair_punctuation
          ? PairedPunctuation(wparam, shifted, state_->settings.punctuation_style)
          : std::wstring{};
  if (!paired_punctuation.empty() ||
      !Punctuation(wparam, shifted, state_->settings.punctuation_style, nullptr).empty()) {
    if (!state_->snapshot.preedit.empty()) {
      const HRESULT composition_result = ApplyKeyResponse(context, VK_SPACE, eaten);
      if (FAILED(composition_result) || *eaten == FALSE) {
        return composition_result;
      }
    }
    if (!paired_punctuation.empty()) {
      return CommitText(context, paired_punctuation, eaten, 1);
    }
    std::wstring punctuation = Punctuation(wparam, shifted, state_->settings.punctuation_style,
                                           &state_->opening_quote);
    return CommitText(context, std::move(punctuation), eaten);
  }
  return ApplyKeyResponse(context, wparam, eaten);
}

void TextService::QueueStartupKey(TfEditCookie cookie, ITfContext* context, WPARAM key,
                                  bool shifted, BOOL* eaten) {
  if (state_->startup_keys.empty()) {
    state_->startup_context = context;
    state_->startup_started_time = GetTickCount64();
    if (!CaptureStartupTarget(cookie, context, true)) {
      CancelStartupReplay(false);
      *eaten = FALSE;
      return;
    }
  }
  if (state_->startup_context.Get() != context) {
    CancelStartupReplay(false);
    *eaten = TRUE;
    return;
  }
  detail::StartupKeyEffect effect = detail::StartupKeyEffect::kNeutral;
  if (IsLetterKey(key) || (!shifted && key == VK_OEM_7)) {
    effect = detail::StartupKeyEffect::kCompose;
  } else if (key == VK_BACK) {
    effect = detail::StartupKeyEffect::kBackspace;
  } else if (key == VK_ESCAPE) {
    effect = detail::StartupKeyEffect::kCancel;
  } else if (key == VK_SPACE || key == VK_RETURN ||
             (!shifted && key >= L'1' && key <= L'9') ||
             (!IsPageKey(key, state_->settings.page_key_set, false) &&
              !IsPageKey(key, state_->settings.page_key_set, true) &&
              (!PairedPunctuation(key, shifted, state_->settings.punctuation_style).empty() ||
               !Punctuation(key, shifted, state_->settings.punctuation_style, nullptr).empty()))) {
    effect = detail::StartupKeyEffect::kCommit;
  }
  if (!state_->startup_keys.Push(static_cast<std::uint32_t>(key), shifted, effect)) {
    // Preserve every already-accepted key through the same-context fallback.
    // The overflow-triggering key is then forwarded only after that synchronous
    // insertion; the 2048-key bound is unreachable during normal startup input.
    OutputDebugStringW(L"Ziliu: startup key queue overflow; using text fallback\n");
    const std::uint64_t generation = state_->startup_keys.generation();
    const HRESULT fallback = FallbackStartupKeys(cookie, context, generation);
    // The existing queue has been inserted synchronously. Let the overflow key
    // reach the host only after that insertion, preserving document order.
    *eaten = FAILED(fallback) ? TRUE : FALSE;
    return;
  }
  *eaten = TRUE;
  if (!ScheduleStartupReplay()) {
    const std::uint64_t generation = state_->startup_keys.generation();
    static_cast<void>(FallbackStartupKeys(cookie, context, generation));
  }
}

HRESULT TextService::ReplayStartupKeys(TfEditCookie cookie, ITfContext* context,
                                       std::uint64_t generation) {
  if (generation != state_->startup_keys.generation()) {
    return S_OK;
  }
  state_->startup_replay_scheduled = false;
  if (state_->startup_context.Get() != context || !IsFocusedContext(context) ||
      !ValidateStartupTarget(cookie, context)) {
    CancelStartupReplay(false);
    return S_OK;
  }
  if (!AllowKeyInContext(context, &cookie)) {
    return S_OK;
  }
  // Privacy authorization can abandon the old session and advance the queue
  // generation. Never let that callback operate on replacement state.
  if (generation != state_->startup_keys.generation() ||
      state_->startup_context.Get() != context) {
    return S_OK;
  }
  if (!EnsureSession()) {
    if (!ScheduleStartupReplay()) {
      return FallbackStartupKeys(cookie, context, generation);
    }
    return S_OK;
  }

  const auto pending = state_->startup_keys.Take(generation);
  const std::size_t batch_size =
      std::min(kStartupReplayBatchSize, pending.size());
  for (std::size_t index = 0; index < batch_size; ++index) {
    const detail::StartupKey& key = pending[index];
    if (generation != state_->startup_keys.generation() ||
        state_->key_context.Get() != context) {
      return S_OK;
    }
    BOOL replay_eaten = FALSE;
    const HRESULT result = ProcessKeyDown(context, static_cast<WPARAM>(key.key),
                                          key.shifted, &replay_eaten);
    if (FAILED(result)) {
      if (!IsFocusedContext(context) || !AllowKeyInContext(context, &cookie)) {
        return S_OK;
      }
      std::wstring fallback;
      bool opening_quote = state_->opening_quote;
      for (std::size_t tail = index; tail < pending.size(); ++tail) {
        AppendStartupFallbackKey(pending[tail], state_->settings.punctuation_style,
                                 &opening_quote, &fallback);
      }
      state_->opening_quote = opening_quote;
      if (fallback.empty()) {
        return S_OK;
      }
      BOOL ignored = FALSE;
      return CommitText(context, std::move(fallback), &ignored);
    }
    if (replay_eaten == FALSE) {
      std::wstring fallback;
      bool opening_quote = state_->opening_quote;
      AppendStartupFallbackKey(key, state_->settings.punctuation_style,
                               &opening_quote, &fallback);
      state_->opening_quote = opening_quote;
      if (!fallback.empty()) {
        BOOL ignored = FALSE;
        const HRESULT fallback_result = CommitText(context, std::move(fallback), &ignored);
        if (FAILED(fallback_result)) {
          return fallback_result;
        }
      }
    }
  }
  for (std::size_t index = batch_size; index < pending.size(); ++index) {
    const detail::StartupKey& key = pending[index];
    if (!state_->startup_keys.Push(key.key, key.shifted, key.effect)) {
      OutputDebugStringW(L"Ziliu: startup replay requeue overflow\n");
      break;
    }
  }
  if (state_->startup_keys.empty()) {
    state_->startup_context.Reset();
    state_->startup_selection.Reset();
    state_->startup_focus = nullptr;
    state_->startup_foreground = nullptr;
    state_->startup_started_time = 0;
  } else {
    if (!CaptureStartupTarget(cookie, context, false)) {
      CancelStartupReplay(false);
      return S_OK;
    }
    if (!ScheduleStartupReplay()) {
      return FallbackStartupKeys(cookie, context, generation);
    }
  }
  return S_OK;
}

HRESULT TextService::FallbackStartupKeys(TfEditCookie cookie, ITfContext* context,
                                         std::uint64_t generation) {
  if (generation != state_->startup_keys.generation()) {
    return S_OK;
  }
  state_->startup_replay_scheduled = false;
  if (state_->startup_context.Get() != context || !IsFocusedContext(context) ||
      !ValidateStartupTarget(cookie, context)) {
    CancelStartupReplay(false);
    return S_OK;
  }
  if (!AllowKeyInContext(context, &cookie)) {
    return S_OK;
  }
  if (generation != state_->startup_keys.generation() ||
      state_->startup_context.Get() != context) {
    return S_OK;
  }

  const auto pending = state_->startup_keys.Take(generation);
  std::wstring text;
  bool opening_quote = state_->opening_quote;
  for (const detail::StartupKey& key : pending) {
    AppendStartupFallbackKey(key, state_->settings.punctuation_style,
                             &opening_quote, &text);
  }
  state_->opening_quote = opening_quote;
  CancelStartupReplay(false);
  if (text.empty()) {
    return S_OK;
  }
  BOOL ignored = FALSE;
  return CommitText(context, std::move(text), &ignored);
}

HRESULT TextService::ToggleInputMode(ITfContext* context, BOOL* eaten,
                                     bool commit_pending_input) {
  // A queued Chinese composition belongs to the old mode and must not be
  // replayed after the user switches to direct input.
  CancelStartupReplay(false);
  if (commit_pending_input && state_->chinese_mode && !state_->snapshot.empty()) {
    const HRESULT commit_result = CommitPendingInput(context, eaten);
    if (FAILED(commit_result)) {
      return commit_result;
    }
  } else {
    ResetCompositionState();
  }
  state_->chinese_mode = !state_->chinese_mode;
  PublishInputMode();
  if (state_->language_bar_button != nullptr) {
    state_->language_bar_button->SetChineseMode(state_->chinese_mode);
  }
  static_cast<void>(context);
  *eaten = TRUE;
  return S_OK;
}

void TextService::ResetCompositionState() {
  if (!state_->snapshot.empty() && state_->session_id != 0) {
    const core::ipc::Request request{state_->request_id++, state_->session_id,
                                     core::ipc::Command::kReset, 0};
    static_cast<void>(state_->client.Exchange(request));
  }
  state_->snapshot = {};
  state_->pending_response = {};
  state_->candidate_page_offset = 0;
  state_->pending_caret_back = 0;
  state_->candidate_window.Hide();
}

HRESULT TextService::HandleCandidatePage(ITfContext* context, bool next, BOOL* eaten) {
  static_cast<void>(context);
  const core::ipc::Request request{
      state_->request_id++, state_->session_id,
      next ? core::ipc::Command::kPageDown : core::ipc::Command::kPageUp, 0};
  const auto response = state_->client.Exchange(request);
  if (response.has_value() && response->status == core::ipc::Status::kOk && response->consumed) {
    state_->snapshot = response->snapshot;
    if (state_->settings.candidate_page_mode == core::CandidatePageMode::kMultiLine) {
      state_->candidate_window.SetExpanded(true);
    }
    ShowCandidateWindow();
  }
  *eaten = TRUE;
  return S_OK;
}

HRESULT TextService::CommitText(ITfContext* context, std::wstring text, BOOL* eaten,
                                std::size_t caret_back) {
  state_->pending_response = {};
  state_->pending_response.commit = std::move(text);
  state_->pending_caret_back = caret_back;
  auto* edit_session = new (std::nothrow) CompositionEditSession(this, context);
  if (edit_session == nullptr) {
    return E_OUTOFMEMORY;
  }
  HRESULT edit_result = E_FAIL;
  const HRESULT request_result = context->RequestEditSession(
      client_id_, edit_session, TF_ES_SYNC | TF_ES_READWRITE, &edit_result);
  edit_session->Release();
  if (FAILED(request_result)) {
    return request_result;
  }
  if (FAILED(edit_result)) {
    return edit_result;
  }
  *eaten = TRUE;
  return S_OK;
}

HRESULT TextService::CommitPendingInput(ITfContext* context, BOOL* eaten) {
  if (context == nullptr || eaten == nullptr) {
    return E_INVALIDARG;
  }
  std::wstring typed_input = state_->snapshot.plain_text();
  ResetCompositionState();
  if (typed_input.empty()) {
    *eaten = TRUE;
    return S_OK;
  }
  return CommitText(context, std::move(typed_input), eaten);
}

HRESULT TextService::ApplyKeyResponse(ITfContext* context, WPARAM wparam, BOOL* eaten) {
  core::ipc::Command command = core::ipc::Command::kInputLetter;
  std::uint32_t value = 0;
  const auto slice = core::MakeCandidatePageSlice(
      state_->snapshot.candidates.size(), state_->settings.candidate_count,
      state_->candidate_page_offset);
  if (IsLetterKey(wparam)) {
    value = static_cast<std::uint32_t>(wparam - L'A' + L'a');
  } else if (wparam == VK_OEM_7) {
    command = core::ipc::Command::kInputSeparator;
  } else if (wparam == VK_BACK) {
    command = core::ipc::Command::kBackspace;
  } else if (wparam == VK_ESCAPE) {
    command = core::ipc::Command::kReset;
  } else if (wparam == VK_SPACE) {
    command = core::ipc::Command::kSelectCandidate;
    const bool highlight_is_visible =
        state_->snapshot.highlighted_index >= slice.offset &&
        state_->snapshot.highlighted_index < slice.offset + slice.count;
    value = static_cast<std::uint32_t>(highlight_is_visible
                                           ? state_->snapshot.highlighted_index - slice.offset
                                           : 0);
  } else if (wparam >= L'1' && wparam <= L'9') {
    command = core::ipc::Command::kSelectCandidate;
    value = static_cast<std::uint32_t>(wparam - L'1');
  } else {
    return S_OK;
  }

  const core::ipc::Request request{state_->request_id++, state_->session_id, command, value};
  const auto response = state_->client.Exchange(request);
  if (!response.has_value() || response->status != core::ipc::Status::kOk ||
      !response->consumed) {
    if (!response.has_value() || response->status == core::ipc::Status::kSessionNotFound) {
      AbandonSession(context);
    }
    return S_OK;
  }

  state_->pending_response = *response;
  state_->pending_caret_back = 0;
  auto* edit_session = new (std::nothrow) CompositionEditSession(this, context);
  if (edit_session == nullptr) {
    AbandonSession(context);
    return S_OK;
  }
  HRESULT edit_result = E_FAIL;
  const HRESULT request_result = context->RequestEditSession(
      client_id_, edit_session, TF_ES_SYNC | TF_ES_READWRITE, &edit_result);
  edit_session->Release();
  if (FAILED(request_result) || FAILED(edit_result)) {
    AbandonSession(context);
    return S_OK;
  }

  state_->snapshot = response->snapshot;
  state_->candidate_window.SetExpanded(false);
  ShowCandidateWindow();
  *eaten = TRUE;
  return S_OK;
}

HRESULT TextService::ApplyCompositionEdit(TfEditCookie edit_cookie, ITfContext* context) {
  using Microsoft::WRL::ComPtr;
  // The host can change scope after the key callback or while an edit is queued.
  const detail::InputPrivacy privacy = detail::ContextBlocksInput(context, activation_flags_)
      ? detail::InputPrivacy::kBlocked : detail::ClassifyScope(context, edit_cookie);
  if (privacy == detail::InputPrivacy::kBlocked || state_->key_context.Get() != context ||
      privacy != state_->key_privacy) {
    ClearCommittedPairCaret();
    AbandonSession(context);
    return E_ACCESSDENIED;
  }
  // A text-store call can reenter TSF. Freeze this edit's payload and consume its
  // caret request before calling the host, rather than rereading mutable state.
  const std::wstring commit = state_->pending_response.commit;
  const std::size_t caret_back = std::exchange(state_->pending_caret_back, 0);
  if (commit.size() > static_cast<std::size_t>(std::numeric_limits<LONG>::max()) ||
      caret_back > commit.size()) {
    return E_INVALIDARG;
  }
  ComPtr<ITfRange> range;
  TF_SELECTION selection{};
  ULONG fetched = 0;
  const HRESULT selection_result =
      context->GetSelection(edit_cookie, TF_DEFAULT_SELECTION, 1, &selection, &fetched);
  if (FAILED(selection_result) || fetched != 1 || selection.range == nullptr) {
    return FAILED(selection_result) ? selection_result : E_FAIL;
  }
  range.Attach(selection.range);

  if (!commit.empty()) {
    HRESULT text_result = E_FAIL;
    if (caret_back != 0) {
      ComPtr<ITfInsertAtSelection> insertion;
      const HRESULT query_result = context->QueryInterface(IID_PPV_ARGS(&insertion));
      if (FAILED(query_result)) {
        return query_result;
      }
      // Use the range returned for the actual insertion, not the old selection
      // whose anchors may have moved while the text store inserted the pair.
      ComPtr<ITfRange> inserted;
      text_result = insertion->InsertTextAtSelection(edit_cookie, 0, commit.data(),
          static_cast<LONG>(commit.size()), inserted.GetAddressOf());
      if (SUCCEEDED(text_result)) {
        range = std::move(inserted);
        if (caret_back == 1 && range != nullptr &&
            SUCCEEDED(range->Clone(state_->committed_pair_range.ReleaseAndGetAddressOf()))) {
          state_->committed_pair_context = context;
          state_->committed_pair = commit;
          state_->committed_pair_focus = GetFocus();
          state_->committed_pair_foreground = GetForegroundWindow();
          state_->committed_pair_time = GetTickCount64();
        }
      }
    } else {
      text_result = range->SetText(edit_cookie, 0, commit.data(), static_cast<LONG>(commit.size()));
    }
    if (FAILED(text_result)) {
      return text_result;
    }
  }

  // The initial shell keeps preedit exclusively in the candidate window.
  // Mutating the host document only on commit avoids relying on host-specific
  // inline-composition behavior while preserving a synchronous TSF insertion.
  if (range != nullptr) {
    ComPtr<ITfContextView> view;
    if (SUCCEEDED(context->GetActiveView(view.GetAddressOf()))) {
      RECT text_rectangle{};
      BOOL clipped = FALSE;
      if (SUCCEEDED(view->GetTextExt(edit_cookie, range.Get(), &text_rectangle, &clipped))) {
        state_->candidate_anchor = text_rectangle;
      }
      static_cast<void>(view->GetWnd(&state_->candidate_owner));
    }
    const HRESULT collapse_result = detail::CollapseInsertedRange(range.Get(), edit_cookie, caret_back);
    if (FAILED(collapse_result)) {
      return collapse_result;
    }
    TF_SELECTION updated_selection{};
    updated_selection.range = range.Get();
    updated_selection.style.ase = TF_AE_END;
    updated_selection.style.fInterimChar = FALSE;
    return context->SetSelection(edit_cookie, 1, &updated_selection);
  }
  return E_FAIL;
}

void TextService::ClearCommittedPairCaret() {
  state_->committed_pair_range.Reset();
  state_->committed_pair_context.Reset();
  state_->committed_pair.clear();
  state_->committed_pair_needs_left = false;
}

HRESULT TextService::VerifyCommittedPairCaret(TfEditCookie cookie, ITfContext* context) {
  using Microsoft::WRL::ComPtr;
  state_->committed_pair_needs_left = false;
  if (!state_->committed_pair_range || context != state_->committed_pair_context.Get() ||
      state_->committed_pair.empty() || state_->committed_pair.size() > 16) {
    return S_FALSE;
  }
  wchar_t text[16]{};
  ULONG read = 0;
  const HRESULT read_result = state_->committed_pair_range->GetText(cookie, 0, text, 16, &read);
  if (FAILED(read_result) ||
      std::wstring_view(text, read) != state_->committed_pair) {
    return S_FALSE;
  }
  TF_SELECTION selection{};
  ULONG fetched = 0;
  if (FAILED(context->GetSelection(cookie, TF_DEFAULT_SELECTION, 1, &selection, &fetched)) ||
      fetched != 1 || selection.range == nullptr) {
    return S_FALSE;
  }
  ComPtr<ITfRange> current;
  current.Attach(selection.range);
  LONG start = 1;
  LONG end = 1;
  if (SUCCEEDED(current->CompareStart(cookie, state_->committed_pair_range.Get(),
                                      TF_ANCHOR_END, &start)) &&
      SUCCEEDED(current->CompareEnd(cookie, state_->committed_pair_range.Get(),
                                    TF_ANCHOR_END, &end)) && start == 0 && end == 0) {
    state_->committed_pair_needs_left = true;
  }
  // Chromium's transitory TSF store can report our requested selection while
  // Blink commits with kMoveCursorAfterText. Do not extend this compatibility
  // path to other text stores merely because they are transitory.
  wchar_t class_name[64]{};
  TF_STATUS status{};
  ComPtr<ITfRange> intended;
  if (!state_->committed_pair_needs_left &&
      GetClassNameW(state_->committed_pair_foreground, class_name, 64) > 0 &&
      std::wstring_view(class_name).starts_with(L"Chrome_WidgetWin_") &&
      SUCCEEDED(context->GetStatus(&status)) &&
      (status.dwStaticFlags & TF_SS_TRANSITORY) != 0 &&
      SUCCEEDED(state_->committed_pair_range->Clone(intended.GetAddressOf())) &&
      SUCCEEDED(detail::CollapseInsertedRange(intended.Get(), cookie, 1)) &&
      SUCCEEDED(current->CompareStart(cookie, intended.Get(), TF_ANCHOR_START, &start)) &&
      SUCCEEDED(current->CompareEnd(cookie, intended.Get(), TF_ANCHOR_END, &end)) &&
      start == 0 && end == 0) {
    state_->committed_pair_needs_left = true;
  }
  return S_OK;
}

void TextService::FinishCommittedPairCaret(ITfContext* context) {
  if (!state_->committed_pair_range) {
    return;
  }
  // Wait for the originating punctuation chord to be released, without a timer,
  // process wait, or global keyboard hook. Native text stores need no fallback.
  if (HasShiftModifier() || HasControlModifier() || HasAltModifier()) {
    return;
  }
  const auto same_focus = [this, context]() {
    return context == state_->committed_pair_context.Get() &&
           state_->committed_pair_focus != nullptr &&
           GetFocus() == state_->committed_pair_focus &&
           GetForegroundWindow() == state_->committed_pair_foreground &&
           GetWindowThreadProcessId(GetForegroundWindow(), nullptr) == GetCurrentThreadId() &&
           GetTickCount64() - state_->committed_pair_time <= 1000;
  };
  if (same_focus()) {
    auto* edit = new (std::nothrow) CompositionEditSession(this, context, true);
    if (edit != nullptr) {
      HRESULT result = E_FAIL;
      const HRESULT requested = context->RequestEditSession(
          client_id_, edit, TF_ES_SYNC | TF_ES_READ, &result);
      edit->Release();
      MSG queued_key{};
      // The originating key-up can still be queued while TSF calls this sink.
      // Only a later key-down starts another user edit.
      const bool later_key =
          PeekMessageW(&queued_key, nullptr, WM_KEYDOWN, WM_KEYDOWN, PM_NOREMOVE) != FALSE ||
          PeekMessageW(&queued_key, nullptr, WM_SYSKEYDOWN, WM_SYSKEYDOWN, PM_NOREMOVE) != FALSE;
      if (SUCCEEDED(requested) && SUCCEEDED(result) &&
          state_->committed_pair_needs_left && same_focus() &&
          !later_key) {
        // Some text stores discard SetSelection when committing an IME string.
        // Correct only an unchanged pair and a validated host selection state.
        // Never enqueue the correction behind another user's pending keystroke.
        INPUT keys[2]{};
        keys[0].type = keys[1].type = INPUT_KEYBOARD;
        keys[0].ki.wVk = keys[1].ki.wVk = VK_LEFT;
        keys[0].ki.dwFlags = KEYEVENTF_EXTENDEDKEY;
        keys[1].ki.dwFlags = KEYEVENTF_EXTENDEDKEY | KEYEVENTF_KEYUP;
        static_cast<void>(SendInput(2, keys, sizeof(INPUT)));
      }
    }
  }
  ClearCommittedPairCaret();
}

STDMETHODIMP TextService::OnTestKeyUp(ITfContext* context, WPARAM wparam, LPARAM lparam,
                                      BOOL* eaten) {
  static_cast<void>(lparam);
  if (eaten == nullptr) {
    return E_INVALIDARG;
  }
  *eaten = FALSE;
  if (!(IsInputModeSwitchKey(wparam) && state_->switch_key_down) &&
      state_->committed_pair_range == nullptr) {
    return S_OK;
  }
  if (!AllowKeyInContext(context)) {
    return S_OK;
  }
  *eaten = (IsInputModeSwitchKey(wparam) && state_->switch_key_down) ||
                   state_->committed_pair_range != nullptr ? TRUE : FALSE;
  return S_OK;
}

STDMETHODIMP TextService::OnKeyUp(ITfContext* context, WPARAM wparam, LPARAM lparam,
                                  BOOL* eaten) {
  static_cast<void>(lparam);
  if (context == nullptr || eaten == nullptr) {
    return E_INVALIDARG;
  }
  *eaten = FALSE;
  if (!(IsInputModeSwitchKey(wparam) && state_->switch_key_down) &&
      state_->committed_pair_range == nullptr) {
    return S_OK;
  }
  return RequestKeyEdit(context, wparam, true, eaten);
}

HRESULT TextService::HandleKeyUp(TfEditCookie cookie, ITfContext* context,
                                WPARAM wparam, BOOL* eaten) {
  if (!AllowKeyInContext(context, &cookie)) {
    return S_OK;
  }
  FinishCommittedPairCaret(context);
  if (!IsInputModeSwitchKey(wparam) || !state_->switch_key_down) {
    return S_OK;
  }
  const bool should_toggle = !state_->switch_key_used;
  state_->switch_key_down = false;
  state_->switch_key_used = false;
  if (!should_toggle) {
    return S_OK;
  }
  return ToggleInputMode(context, eaten, true);
}

STDMETHODIMP TextService::OnPreservedKey(ITfContext* context, REFGUID guid, BOOL* eaten) {
  static_cast<void>(context);
  static_cast<void>(guid);
  if (eaten == nullptr) {
    return E_INVALIDARG;
  }
  *eaten = FALSE;
  return S_OK;
}

}  // namespace ziliu::tsf
