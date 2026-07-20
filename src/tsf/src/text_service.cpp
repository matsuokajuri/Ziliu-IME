#include "ziliu/tsf/text_service.h"

#include "ziliu/core/ipc_protocol.h"
#include "ziliu/core/settings.h"
#include "ziliu/ipc/pipe_client.h"
#include "ziliu/tsf/language_bar_button.h"
#include "ziliu/tsf/module_state.h"
#include "ziliu/ui/candidate_window.h"

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

struct TextServiceState {
  ipc::PipeClient client;
  std::uint64_t request_id = 1;
  std::uint64_t session_id = 0;
  core::ipc::Response pending_response;
  core::CompositionSnapshot snapshot;
  ui::CandidateWindow candidate_window;
  ITfLangBarItemMgr* language_bar_manager = nullptr;
  LanguageBarButton* language_bar_button = nullptr;
  POINT candidate_anchor{};
  HWND candidate_owner = nullptr;
  core::Settings settings;
  std::filesystem::file_time_type settings_write_time{};
  std::size_t candidate_page_offset = 0;
  bool broker_started = false;
  bool settings_file_known = false;
  bool chinese_mode = true;
  bool switch_key_down = false;
  bool switch_key_used = false;
  bool opening_quote = true;
};

class CompositionEditSession final : public ITfEditSession {
 public:
  CompositionEditSession(TextService* service, ITfContext* context)
      : service_(service), context_(context) {
    service_->AddRef();
    context_->AddRef();
  }

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
};

namespace {

bool HasAltModifier() { return (GetKeyState(VK_MENU) & 0x8000) != 0; }

bool HasControlModifier() { return (GetKeyState(VK_CONTROL) & 0x8000) != 0; }

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

std::wstring FullWidthPunctuation(WPARAM key, bool* opening_quote) {
  switch (key) {
    case VK_OEM_COMMA:
      return L"，";
    case VK_OEM_PERIOD:
      return L"。";
    case VK_OEM_1:
      return L"；";
    case VK_OEM_2:
      return L"？";
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
    default:
      return {};
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

  ITfLangBarItemMgr* language_bar_manager = nullptr;
  if (SUCCEEDED(thread_manager_->QueryInterface(IID_PPV_ARGS(&language_bar_manager)))) {
    const auto settings_path = SettingsExecutablePath();
    auto* language_bar_button = new (std::nothrow)
        LanguageBarButton(settings_path.has_value() ? settings_path->native() : std::wstring{});
    if (language_bar_button != nullptr &&
        SUCCEEDED(language_bar_manager->AddItem(language_bar_button))) {
      state_->language_bar_manager = language_bar_manager;
      state_->language_bar_button = language_bar_button;
      state_->language_bar_button->SetChineseMode(state_->chinese_mode);
      static_cast<void>(state_->language_bar_button->Show(TRUE));
    } else {
      if (language_bar_button != nullptr) {
        language_bar_button->Release();
      }
      language_bar_manager->Release();
    }
  }

  RefreshSettings(true);
  PublishInputMode();
  StartBroker();
  static_cast<void>(EnsureSession());
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
  state_->broker_started = true;
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
    CloseHandle(process_info.hThread);
    CloseHandle(process_info.hProcess);
  }
}

bool TextService::EnsureSession() {
  if (state_->session_id != 0) {
    return true;
  }
  StartBroker();
  const core::ipc::Request request{state_->request_id++, 0,
                                   core::ipc::Command::kCreateSession, 0};
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
  return true;
}

void TextService::RefreshSettings(bool force) {
  const auto path = SettingsPath();
  if (!path.has_value()) {
    return;
  }

  std::error_code time_error;
  const auto write_time = std::filesystem::last_write_time(*path, time_error);
  if (time_error) {
    if (force || state_->settings_file_known) {
      const bool was_traditional =
          state_->settings.character_set == core::CharacterSet::kTraditional;
      state_->settings = {};
      state_->settings_file_known = false;
      state_->settings_write_time = {};
      state_->candidate_page_offset = 0;
      if (state_->session_id != 0 && was_traditional) {
        const core::ipc::Request request{state_->request_id++, state_->session_id,
                                         core::ipc::Command::kSetTraditional, 0U};
        static_cast<void>(state_->client.Exchange(request));
      }
    }
    return;
  }
  if (!force && state_->settings_file_known && write_time == state_->settings_write_time) {
    return;
  }

  const auto contents = ReadSettingsFile(*path);
  if (!contents.has_value()) {
    return;
  }
  const core::CharacterSet previous_character_set = state_->settings.character_set;
  state_->settings = core::ParseSettings(*contents);
  state_->settings_write_time = write_time;
  state_->settings_file_known = true;
  state_->candidate_page_offset = 0;

  if (state_->session_id != 0 && previous_character_set != state_->settings.character_set) {
    const core::ipc::Request request{
        state_->request_id++, state_->session_id, core::ipc::Command::kSetTraditional,
        state_->settings.character_set == core::CharacterSet::kTraditional ? 1U : 0U};
    static_cast<void>(state_->client.Exchange(request));
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
}

void TextService::ResetRuntimeState() {
  state_->session_id = 0;
  state_->snapshot = {};
  state_->pending_response = {};
  state_->candidate_page_offset = 0;
  state_->switch_key_down = false;
  state_->switch_key_used = false;
  state_->candidate_window.Hide();
  state_->broker_started = false;
}

void TextService::AbandonSession(ITfContext* context) {
  static_cast<void>(context);
  if (state_->session_id != 0) {
    const core::ipc::Request close_request{state_->request_id++, state_->session_id,
                                           core::ipc::Command::kCloseSession, 0};
    static_cast<void>(state_->client.Exchange(close_request));
  }
  state_->session_id = 0;
  state_->snapshot = {};
  state_->pending_response = {};
  state_->candidate_page_offset = 0;
  state_->candidate_window.Hide();
  state_->broker_started = false;
}

bool TextService::IsInputModeSwitchKey(WPARAM wparam) const {
  if (state_->settings.input_mode_switch_key == core::InputModeSwitchKey::kControl) {
    return wparam == VK_CONTROL || wparam == VK_LCONTROL || wparam == VK_RCONTROL;
  }
  return wparam == VK_SHIFT || wparam == VK_LSHIFT || wparam == VK_RSHIFT;
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
  if (wparam == VK_BACK || wparam == VK_ESCAPE) {
    return !state_->snapshot.preedit.empty();
  }
  if (wparam == VK_SPACE) {
    return !state_->snapshot.candidates.empty();
  }
  if (wparam >= L'1' && wparam <= L'9') {
    const auto slice = core::MakeCandidatePageSlice(
        state_->snapshot.candidates.size(), state_->settings.candidate_count,
        state_->candidate_page_offset);
    const auto index = static_cast<std::size_t>(wparam - L'1');
    return index < slice.count;
  }
  if (!state_->snapshot.preedit.empty() &&
      (IsPageKey(wparam, state_->settings.page_key_set, false) ||
       IsPageKey(wparam, state_->settings.page_key_set, true))) {
    return true;
  }
  if (state_->snapshot.preedit.empty() &&
      state_->settings.punctuation_style == core::PunctuationStyle::kFullWidth) {
    return !FullWidthPunctuation(wparam, nullptr).empty();
  }
  return false;
}

void TextService::ShowCandidateWindow() {
  if (state_->snapshot.empty()) {
    state_->candidate_window.Hide();
  } else if (state_->candidate_window.Create(state_->candidate_owner)) {
    state_->candidate_window.Show(state_->snapshot, state_->candidate_anchor, state_->settings,
                                  state_->candidate_page_offset);
  }
}

STDMETHODIMP TextService::OnSetFocus(BOOL foreground) {
  if (foreground) {
    PublishInputMode();
  } else {
    state_->candidate_window.Hide();
  }
  return S_OK;
}

STDMETHODIMP TextService::OnTestKeyDown(ITfContext* context, WPARAM wparam, LPARAM lparam,
                                        BOOL* eaten) {
  static_cast<void>(context);
  static_cast<void>(lparam);
  if (eaten == nullptr) {
    return E_INVALIDARG;
  }
  if (state_->snapshot.empty()) {
    RefreshSettings(false);
  }
  if (state_->switch_key_down && !IsInputModeSwitchKey(wparam)) {
    state_->switch_key_used = true;
  }
  if (!ShouldHandleKey(wparam)) {
    *eaten = FALSE;
    return S_OK;
  }
  *eaten = IsInputModeSwitchKey(wparam) || EnsureSession() ? TRUE : FALSE;
  return S_OK;
}

STDMETHODIMP TextService::OnKeyDown(ITfContext* context, WPARAM wparam, LPARAM lparam,
                                    BOOL* eaten) {
  static_cast<void>(lparam);
  if (context == nullptr || eaten == nullptr) {
    return E_INVALIDARG;
  }
  *eaten = FALSE;
  if (IsInputModeSwitchKey(wparam)) {
    state_->switch_key_down = true;
    state_->switch_key_used = false;
    *eaten = TRUE;
    return S_OK;
  }
  if (state_->switch_key_down) {
    state_->switch_key_used = true;
  }
  if (!EnsureSession() || !ShouldHandleKey(wparam)) {
    return S_OK;
  }
  if (!state_->snapshot.preedit.empty()) {
    if (IsPageKey(wparam, state_->settings.page_key_set, false)) {
      return HandleCandidatePage(context, false, eaten);
    }
    if (IsPageKey(wparam, state_->settings.page_key_set, true)) {
      return HandleCandidatePage(context, true, eaten);
    }
  } else if (state_->settings.punctuation_style == core::PunctuationStyle::kFullWidth) {
    std::wstring punctuation = FullWidthPunctuation(wparam, &state_->opening_quote);
    if (!punctuation.empty()) {
      return CommitText(context, std::move(punctuation), eaten);
    }
  }
  return ApplyKeyResponse(context, wparam, eaten);
}

HRESULT TextService::ToggleInputMode(ITfContext* context, BOOL* eaten) {
  if (!state_->snapshot.empty() && state_->session_id != 0) {
    const core::ipc::Request request{state_->request_id++, state_->session_id,
                                     core::ipc::Command::kReset, 0};
    static_cast<void>(state_->client.Exchange(request));
  }
  state_->snapshot = {};
  state_->pending_response = {};
  state_->candidate_page_offset = 0;
  state_->candidate_window.Hide();
  state_->chinese_mode = !state_->chinese_mode;
  PublishInputMode();
  if (state_->language_bar_button != nullptr) {
    state_->language_bar_button->SetChineseMode(state_->chinese_mode);
  }
  static_cast<void>(context);
  *eaten = TRUE;
  return S_OK;
}

HRESULT TextService::HandleCandidatePage(ITfContext* context, bool next, BOOL* eaten) {
  static_cast<void>(context);
  const auto slice = core::MakeCandidatePageSlice(
      state_->snapshot.candidates.size(), state_->settings.candidate_count,
      state_->candidate_page_offset);
  if (next && slice.offset + slice.count < state_->snapshot.candidates.size()) {
    state_->candidate_page_offset = slice.offset + state_->settings.candidate_count;
    ShowCandidateWindow();
    *eaten = TRUE;
    return S_OK;
  }
  if (!next && slice.offset != 0) {
    state_->candidate_page_offset =
        slice.offset > state_->settings.candidate_count
            ? slice.offset - state_->settings.candidate_count
            : 0;
    ShowCandidateWindow();
    *eaten = TRUE;
    return S_OK;
  }

  const core::ipc::Request request{
      state_->request_id++, state_->session_id,
      next ? core::ipc::Command::kPageDown : core::ipc::Command::kPageUp, 0};
  const auto response = state_->client.Exchange(request);
  if (response.has_value() && response->status == core::ipc::Status::kOk && response->consumed) {
    state_->snapshot = response->snapshot;
    if (next) {
      state_->candidate_page_offset = 0;
    } else {
      state_->candidate_page_offset = core::MakeCandidatePageSlice(
                                          state_->snapshot.candidates.size(),
                                          state_->settings.candidate_count,
                                          state_->snapshot.candidates.size())
                                          .offset;
    }
    ShowCandidateWindow();
  }
  *eaten = TRUE;
  return S_OK;
}

HRESULT TextService::CommitText(ITfContext* context, std::wstring text, BOOL* eaten) {
  state_->pending_response = {};
  state_->pending_response.commit = std::move(text);
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

HRESULT TextService::ApplyKeyResponse(ITfContext* context, WPARAM wparam, BOOL* eaten) {
  core::ipc::Command command = core::ipc::Command::kInputLetter;
  std::uint32_t value = 0;
  const auto slice = core::MakeCandidatePageSlice(
      state_->snapshot.candidates.size(), state_->settings.candidate_count,
      state_->candidate_page_offset);
  if (IsLetterKey(wparam)) {
    value = static_cast<std::uint32_t>(wparam - L'A' + L'a');
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
                                           ? state_->snapshot.highlighted_index
                                           : slice.offset);
  } else if (wparam >= L'1' && wparam <= L'9') {
    command = core::ipc::Command::kSelectCandidate;
    value = static_cast<std::uint32_t>(slice.offset + static_cast<std::size_t>(wparam - L'1'));
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
  if (command == core::ipc::Command::kInputLetter ||
      command == core::ipc::Command::kBackspace || state_->snapshot.empty()) {
    state_->candidate_page_offset = 0;
  }
  ShowCandidateWindow();
  *eaten = TRUE;
  return S_OK;
}

HRESULT TextService::ApplyCompositionEdit(TfEditCookie edit_cookie, ITfContext* context) {
  using Microsoft::WRL::ComPtr;
  const auto& response = state_->pending_response;
  ComPtr<ITfRange> range;
  TF_SELECTION selection{};
  ULONG fetched = 0;
  const HRESULT selection_result =
      context->GetSelection(edit_cookie, TF_DEFAULT_SELECTION, 1, &selection, &fetched);
  if (FAILED(selection_result) || fetched != 1 || selection.range == nullptr) {
    return FAILED(selection_result) ? selection_result : E_FAIL;
  }
  range.Attach(selection.range);

  if (!response.commit.empty()) {
    const HRESULT text_result = range->SetText(edit_cookie, 0, response.commit.data(),
                                               static_cast<LONG>(response.commit.size()));
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
        state_->candidate_anchor = POINT{text_rectangle.left, text_rectangle.bottom + 2};
      }
      static_cast<void>(view->GetWnd(&state_->candidate_owner));
    }
    const HRESULT collapse_result = range->Collapse(edit_cookie, TF_ANCHOR_END);
    if (SUCCEEDED(collapse_result)) {
      TF_SELECTION updated_selection{};
      updated_selection.range = range.Get();
      updated_selection.style.ase = TF_AE_NONE;
      updated_selection.style.fInterimChar = FALSE;
      static_cast<void>(context->SetSelection(edit_cookie, 1, &updated_selection));
    }
  }
  return S_OK;
}

STDMETHODIMP TextService::OnTestKeyUp(ITfContext* context, WPARAM wparam, LPARAM lparam,
                                      BOOL* eaten) {
  static_cast<void>(context);
  static_cast<void>(lparam);
  if (eaten == nullptr) {
    return E_INVALIDARG;
  }
  *eaten = IsInputModeSwitchKey(wparam) && state_->switch_key_down ? TRUE : FALSE;
  return S_OK;
}

STDMETHODIMP TextService::OnKeyUp(ITfContext* context, WPARAM wparam, LPARAM lparam,
                                  BOOL* eaten) {
  static_cast<void>(lparam);
  if (context == nullptr || eaten == nullptr) {
    return E_INVALIDARG;
  }
  *eaten = FALSE;
  if (!IsInputModeSwitchKey(wparam) || !state_->switch_key_down) {
    return S_OK;
  }
  const bool should_toggle = !state_->switch_key_used;
  state_->switch_key_down = false;
  state_->switch_key_used = false;
  if (!should_toggle) {
    return S_OK;
  }
  return ToggleInputMode(context, eaten);
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
