#include "ziliu/tsf/text_service.h"

#include "ziliu/core/ipc_protocol.h"
#include "ziliu/ipc/pipe_client.h"
#include "ziliu/tsf/module_state.h"
#include "ziliu/ui/candidate_window.h"

#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
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
  POINT candidate_anchor{};
  HWND candidate_owner = nullptr;
  bool broker_started = false;
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

bool HasControlModifier() {
  return (GetKeyState(VK_CONTROL) & 0x8000) != 0 || (GetKeyState(VK_MENU) & 0x8000) != 0;
}

bool IsLetterKey(WPARAM key) { return key >= L'A' && key <= L'Z'; }

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
  return true;
}

void TextService::ResetRuntimeState() {
  state_->session_id = 0;
  state_->snapshot = {};
  state_->pending_response = {};
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
  state_->candidate_window.Hide();
  state_->broker_started = false;
}

bool TextService::ShouldHandleKey(WPARAM wparam) const {
  if (HasControlModifier()) {
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
    const auto index = static_cast<std::size_t>(wparam - L'1');
    return index < state_->snapshot.candidates.size();
  }
  return false;
}

STDMETHODIMP TextService::OnSetFocus(BOOL foreground) {
  if (!foreground) {
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
  *eaten = EnsureSession() && ShouldHandleKey(wparam) ? TRUE : FALSE;
  return S_OK;
}

STDMETHODIMP TextService::OnKeyDown(ITfContext* context, WPARAM wparam, LPARAM lparam,
                                    BOOL* eaten) {
  static_cast<void>(lparam);
  if (context == nullptr || eaten == nullptr) {
    return E_INVALIDARG;
  }
  *eaten = FALSE;
  if (!EnsureSession() || !ShouldHandleKey(wparam)) {
    return S_OK;
  }
  return ApplyKeyResponse(context, wparam, eaten);
}

HRESULT TextService::ApplyKeyResponse(ITfContext* context, WPARAM wparam, BOOL* eaten) {
  core::ipc::Command command = core::ipc::Command::kInputLetter;
  std::uint32_t value = 0;
  if (IsLetterKey(wparam)) {
    value = static_cast<std::uint32_t>(wparam - L'A' + L'a');
  } else if (wparam == VK_BACK) {
    command = core::ipc::Command::kBackspace;
  } else if (wparam == VK_ESCAPE) {
    command = core::ipc::Command::kReset;
  } else if (wparam == VK_SPACE) {
    command = core::ipc::Command::kSelectCandidate;
    value = static_cast<std::uint32_t>(state_->snapshot.highlighted_index);
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
  if (state_->snapshot.empty()) {
    state_->candidate_window.Hide();
  } else if (state_->candidate_window.Create(state_->candidate_owner)) {
    state_->candidate_window.Show(state_->snapshot, state_->candidate_anchor);
  }
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
  static_cast<void>(wparam);
  static_cast<void>(lparam);
  if (eaten == nullptr) {
    return E_INVALIDARG;
  }
  *eaten = FALSE;
  return S_OK;
}

STDMETHODIMP TextService::OnKeyUp(ITfContext* context, WPARAM wparam, LPARAM lparam,
                                  BOOL* eaten) {
  return OnTestKeyUp(context, wparam, lparam, eaten);
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
