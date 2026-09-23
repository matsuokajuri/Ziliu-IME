#include <windows.h>

#include <initguid.h>
#include <inputscope.h>
#include <msctf.h>
#include <richedit.h>
#include <shellapi.h>

#include "../src/tsf/include/ziliu/tsf/guids.h"

#include <atomic>
#include <array>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr wchar_t kWindowClass[] = L"ZiliuAlphaInputScopeHost";
constexpr wchar_t kWindowTitle[] = L"Ziliu TEST ONLY - InputScope host fixture";
constexpr int kExportButtonId = 100;

struct ContextDiagnostic {
  HRESULT thread_manager_get_focus = E_PENDING;
  HRESULT document_manager_get_top = E_PENDING;
  HRESULT request_edit_session = E_PENDING;
  HRESULT edit_session = E_PENDING;
  HRESULT get_selection = E_PENDING;
  HRESULT clone_range = E_PENDING;
  HRESULT collapse_range = E_PENDING;
  HRESULT get_app_property = E_PENDING;
  HRESULT get_value = E_PENDING;
  HRESULT query_input_scope = E_PENDING;
  HRESULT get_input_scopes = E_PENDING;
  bool focus_matches_edit = false;
  std::vector<int> scope_values;
};

struct Field {
  std::wstring_view label;
  std::wstring_view scope_name;
  std::optional<InputScope> scope;
  DWORD edit_style;
  HWND edit = nullptr;
  HRESULT set_scope_result = S_FALSE;
  bool scope_applied = false;
  ContextDiagnostic context;
};

struct AppState {
  std::filesystem::path output_path;
  HWND status = nullptr;
  HMODULE rich_edit_module = nullptr;
  ITfThreadMgr* thread_manager = nullptr;
  TfClientId client_id = TF_CLIENTID_NULL;
  HRESULT thread_manager_create = E_PENDING;
  HRESULT thread_manager_activate = E_PENDING;
  std::array<Field, 6> fields = {{{L"Ordinary (IS_DEFAULT)", L"IS_DEFAULT", IS_DEFAULT, 0},
                                  {L"Private (IS_PRIVATE)", L"IS_PRIVATE", IS_PRIVATE, 0},
                                  {L"Password (IS_PASSWORD)", L"IS_PASSWORD", IS_PASSWORD,
                                   ES_PASSWORD},
                                  {L"PIN (IS_NUMERIC_PIN)", L"IS_NUMERIC_PIN", IS_NUMERIC_PIN,
                                   ES_PASSWORD},
                                  {L"Unspecified scope", L"UNSPECIFIED", std::nullopt, 0},
                                  {L"Second ordinary (IS_DEFAULT)", L"IS_DEFAULT", IS_DEFAULT,
                                   0}}};
};

class ScopeReadSession final : public ITfEditSession {
 public:
  ScopeReadSession(Field* field, ITfContext* context) : field_(field), context_(context) {
    context_->AddRef();
  }

  STDMETHODIMP QueryInterface(REFIID interface_id, void** object) override {
    if (object == nullptr) return E_INVALIDARG;
    *object = nullptr;
    if (interface_id == IID_IUnknown || interface_id == IID_ITfEditSession) {
      *object = static_cast<ITfEditSession*>(this);
      AddRef();
      return S_OK;
    }
    return E_NOINTERFACE;
  }
  STDMETHODIMP_(ULONG) AddRef() override { return ++references_; }
  STDMETHODIMP_(ULONG) Release() override {
    const ULONG remaining = --references_;
    if (remaining == 0) delete this;
    return remaining;
  }
  STDMETHODIMP DoEditSession(TfEditCookie cookie) override {
    ContextDiagnostic& diagnostic = field_->context;
    TF_SELECTION selection{};
    ULONG fetched = 0;
    diagnostic.get_selection = context_->GetSelection(cookie, TF_DEFAULT_SELECTION, 1, &selection, &fetched);
    if (FAILED(diagnostic.get_selection) || fetched != 1 || selection.range == nullptr) return S_OK;
    ITfRange* range = selection.range;
    ITfRange* collapsed = nullptr;
    diagnostic.clone_range = range->Clone(&collapsed);
    range->Release();
    if (FAILED(diagnostic.clone_range) || collapsed == nullptr) return S_OK;
    diagnostic.collapse_range = collapsed->Collapse(
        cookie, selection.style.ase == TF_AE_START ? TF_ANCHOR_START : TF_ANCHOR_END);
    if (FAILED(diagnostic.collapse_range)) {
      collapsed->Release();
      return S_OK;
    }
    ITfReadOnlyProperty* property = nullptr;
    diagnostic.get_app_property = context_->GetAppProperty(GUID_PROP_INPUTSCOPE, &property);
    if (SUCCEEDED(diagnostic.get_app_property) && property != nullptr) {
      VARIANT value;
      VariantInit(&value);
      diagnostic.get_value = property->GetValue(cookie, collapsed, &value);
      if (SUCCEEDED(diagnostic.get_value) && value.vt == VT_UNKNOWN && value.punkVal != nullptr) {
        ITfInputScope* scope = nullptr;
        diagnostic.query_input_scope = value.punkVal->QueryInterface(IID_PPV_ARGS(&scope));
        if (SUCCEEDED(diagnostic.query_input_scope) && scope != nullptr) {
          InputScope* values = nullptr;
          UINT count = 0;
          diagnostic.get_input_scopes = scope->GetInputScopes(&values, &count);
          if (SUCCEEDED(diagnostic.get_input_scopes) && values != nullptr) {
            for (UINT index = 0; index < count; ++index) {
              diagnostic.scope_values.push_back(static_cast<int>(values[index]));
            }
          }
          CoTaskMemFree(values);
          scope->Release();
        }
      }
      VariantClear(&value);
      property->Release();
    }
    collapsed->Release();
    return S_OK;
  }

 private:
  ~ScopeReadSession() { context_->Release(); }
  std::atomic<ULONG> references_{1};
  Field* field_;
  ITfContext* context_ = nullptr;
};

enum class Mode { kFields, kAuditRegistration };

struct Arguments {
  Mode mode;
  std::filesystem::path output_path;
};

using SetInputScopeFunction = HRESULT(WINAPI*)(HWND, InputScope);

std::wstring HresultText(HRESULT result) {
  wchar_t buffer[16]{};
  swprintf_s(buffer, L"0x%08lX", static_cast<unsigned long>(result));
  return buffer;
}

std::wstring GuidText(REFGUID guid) {
  wchar_t buffer[40]{};
  return StringFromGUID2(guid, buffer, static_cast<int>(std::size(buffer))) > 0
      ? std::wstring(buffer)
      : L"";
}

std::wstring JsonEscape(std::wstring_view input) {
  std::wstring escaped;
  escaped.reserve(input.size());
  for (const wchar_t character : input) {
    switch (character) {
      case L'\\': escaped += L"\\\\"; break;
      case L'\"': escaped += L"\\\""; break;
      case L'\n': escaped += L"\\n"; break;
      case L'\r': escaped += L"\\r"; break;
      case L'\t': escaped += L"\\t"; break;
      default:
        if (character < 0x20 || character > 0x7e) {
          wchar_t buffer[7]{};
          swprintf_s(buffer, L"\\u%04X", static_cast<unsigned int>(character));
          escaped += buffer;
        } else {
          escaped += character;
        }
    }
  }
  return escaped;
}

std::vector<unsigned int> ToCodepoints(std::wstring_view text) {
  std::vector<unsigned int> codepoints;
  for (std::size_t index = 0; index < text.size(); ++index) {
    unsigned int value = text[index];
    if (value >= 0xD800 && value <= 0xDBFF && index + 1 < text.size()) {
      const unsigned int low = text[index + 1];
      if (low >= 0xDC00 && low <= 0xDFFF) {
        value = 0x10000 + ((value - 0xD800) << 10) + (low - 0xDC00);
        ++index;
      }
    }
    codepoints.push_back(value);
  }
  return codepoints;
}

std::wstring ReadOwnedEditText(HWND edit) {
  const LRESULT length = SendMessageW(edit, WM_GETTEXTLENGTH, 0, 0);
  if (length < 0 || length > 4096) return L"";
  std::wstring text(static_cast<std::size_t>(length), L'\0');
  if (length != 0) {
    SendMessageW(edit, WM_GETTEXT, static_cast<WPARAM>(text.size() + 1),
                 reinterpret_cast<LPARAM>(text.data()));
  }
  return text;
}

std::wstring FocusName(const AppState& state, HWND focus) {
  for (const Field& field : state.fields) {
    if (field.edit == focus) return std::wstring(field.label);
  }
  return focus == nullptr ? L"none" : L"test-window-control";
}

Field* FindField(AppState* state, HWND edit) {
  for (Field& field : state->fields) {
    if (field.edit == edit) return &field;
  }
  return nullptr;
}

void CaptureFocusedContext(AppState* state, HWND edit) {
  Field* field = FindField(state, edit);
  if (field == nullptr) return;
  field->context = ContextDiagnostic{};
  field->context.focus_matches_edit = GetFocus() == edit;
  if (state->thread_manager == nullptr || state->client_id == TF_CLIENTID_NULL) return;
  ITfDocumentMgr* document_manager = nullptr;
  field->context.thread_manager_get_focus = state->thread_manager->GetFocus(&document_manager);
  if (FAILED(field->context.thread_manager_get_focus) || document_manager == nullptr) return;
  ITfContext* context = nullptr;
  field->context.document_manager_get_top = document_manager->GetTop(&context);
  document_manager->Release();
  if (FAILED(field->context.document_manager_get_top) || context == nullptr) return;
  auto* session = new (std::nothrow) ScopeReadSession(field, context);
  if (session == nullptr) {
    context->Release();
    field->context.request_edit_session = E_OUTOFMEMORY;
    return;
  }
  HRESULT session_result = E_FAIL;
  field->context.request_edit_session = context->RequestEditSession(
      state->client_id, session, TF_ES_SYNC | TF_ES_READ, &session_result);
  field->context.edit_session = session_result;
  session->Release();
  context->Release();
}

void WriteContextDiagnostic(std::wostream& output, const ContextDiagnostic& diagnostic) {
  output << L"\"focusMatchesEdit\": " << (diagnostic.focus_matches_edit ? L"true" : L"false")
         << L", \"threadMgrGetFocus\": \"" << HresultText(diagnostic.thread_manager_get_focus)
         << L"\", \"documentMgrGetTop\": \"" << HresultText(diagnostic.document_manager_get_top)
         << L"\", \"requestEditSession\": \"" << HresultText(diagnostic.request_edit_session)
         << L"\", \"editSession\": \"" << HresultText(diagnostic.edit_session)
         << L"\", \"getSelection\": \"" << HresultText(diagnostic.get_selection)
         << L"\", \"cloneRange\": \"" << HresultText(diagnostic.clone_range)
         << L"\", \"collapseRange\": \"" << HresultText(diagnostic.collapse_range)
         << L"\", \"getAppProperty\": \"" << HresultText(diagnostic.get_app_property)
         << L"\", \"getValue\": \"" << HresultText(diagnostic.get_value)
         << L"\", \"queryInputScope\": \"" << HresultText(diagnostic.query_input_scope)
         << L"\", \"getInputScopes\": \"" << HresultText(diagnostic.get_input_scopes)
         << L"\", \"scopeValues\": [";
  for (std::size_t index = 0; index < diagnostic.scope_values.size(); ++index) {
    output << diagnostic.scope_values[index];
    if (index + 1 != diagnostic.scope_values.size()) output << L", ";
  }
  output << L']';
}

bool Export(const AppState& state, std::wstring* error) {
  wchar_t focus_class[256]{};
  const HWND focus = GetFocus();
  if (focus != nullptr) GetClassNameW(focus, focus_class, static_cast<int>(std::size(focus_class)));

  std::wofstream output(state.output_path, std::ios::binary | std::ios::trunc);
  if (!output.is_open()) {
    *error = L"Could not write the caller-specified export path.";
    return false;
  }
  output << L"{\n  \"testOnly\": true,\n  \"windowTitle\": \""
         << JsonEscape(kWindowTitle) << L"\",\n  \"windowClass\": \""
         << JsonEscape(kWindowClass) << L"\",\n  \"focus\": {\"window\": \""
         << JsonEscape(FocusName(state, focus)) << L"\", \"class\": \""
         << JsonEscape(focus_class) << L"\"},\n  \"threadManagerCreateHresult\": \""
         << HresultText(state.thread_manager_create) << L"\",\n  \"threadManagerActivateHresult\": \""
         << HresultText(state.thread_manager_activate) << L"\",\n  \"fields\": [\n";
  for (std::size_t index = 0; index < state.fields.size(); ++index) {
    const Field& field = state.fields[index];
    const std::wstring text = ReadOwnedEditText(field.edit);
    output << L"    {\"label\": \"" << JsonEscape(field.label) << L"\", \"scope\": \""
           << JsonEscape(field.scope_name) << L"\", \"setInputScopeHresult\": \""
           << (field.scope_applied ? HresultText(field.set_scope_result) : L"NOT_APPLIED")
           << L"\", \"value\": \"" << JsonEscape(text) << L"\", \"codepoints\": [";
    const std::vector<unsigned int> codepoints = ToCodepoints(text);
    for (std::size_t codepoint_index = 0; codepoint_index < codepoints.size(); ++codepoint_index) {
      wchar_t codepoint[12]{};
      swprintf_s(codepoint, L"U+%04X", codepoints[codepoint_index]);
      output << L'\"' << codepoint << L'\"';
      if (codepoint_index + 1 != codepoints.size()) output << L", ";
    }
    output << L"], \"focusedContext\": {";
    WriteContextDiagnostic(output, field.context);
    output << L"}}" << (index + 1 == state.fields.size() ? L"\n" : L",\n");
  }
  output << L"  ]\n}\n";
  if (!output.good()) {
    *error = L"Writing the caller-specified export path failed.";
    return false;
  }
  return true;
}

bool AuditRegistration(const std::filesystem::path& output_path, std::wstring* error) {
  const HRESULT initialize_result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  const bool uninitialize = SUCCEEDED(initialize_result);
  ITfCategoryMgr* category_manager = nullptr;
  const HRESULT category_create = CoCreateInstance(CLSID_TF_CategoryMgr, nullptr,
                                                   CLSCTX_INPROC_SERVER,
                                                   IID_PPV_ARGS(&category_manager));
  IEnumGUID* category_enumerator = nullptr;
  const HRESULT category_enumerate = SUCCEEDED(category_create)
      ? category_manager->EnumCategoriesInItem(ziliu::tsf::kTextServiceClsid, &category_enumerator)
      : category_create;
  std::vector<GUID> categories;
  HRESULT category_terminal = category_enumerate;
  if (SUCCEEDED(category_enumerate) && category_enumerator != nullptr) {
    GUID category{};
    ULONG fetched = 0;
    for (;;) {
      const HRESULT next = category_enumerator->Next(1, &category, &fetched);
      if (next != S_OK || fetched != 1) {
        category_terminal = next;
        break;
      }
      categories.push_back(category);
    }
  } else if (SUCCEEDED(category_enumerate)) {
    category_terminal = E_UNEXPECTED;
  }
  if (category_enumerator != nullptr) category_enumerator->Release();
  if (category_manager != nullptr) category_manager->Release();

  ITfInputProcessorProfiles* profiles = nullptr;
  const HRESULT profiles_create = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr,
                                                   CLSCTX_INPROC_SERVER,
                                                   IID_PPV_ARGS(&profiles));
  IEnumGUID* processor_enumerator = nullptr;
  const HRESULT processor_enumerate = SUCCEEDED(profiles_create)
      ? profiles->EnumInputProcessorInfo(&processor_enumerator)
      : profiles_create;
  bool service_listed = false;
  HRESULT processor_terminal = processor_enumerate;
  if (SUCCEEDED(processor_enumerate) && processor_enumerator != nullptr) {
    GUID service{};
    ULONG fetched = 0;
    for (;;) {
      const HRESULT next = processor_enumerator->Next(1, &service, &fetched);
      if (next != S_OK || fetched != 1) {
        processor_terminal = next;
        break;
      }
      if (IsEqualGUID(service, ziliu::tsf::kTextServiceClsid)) service_listed = true;
    }
  } else if (SUCCEEDED(processor_enumerate)) {
    processor_terminal = E_UNEXPECTED;
  }
  if (processor_enumerator != nullptr) processor_enumerator->Release();

  IEnumTfLanguageProfiles* profile_enumerator = nullptr;
  const HRESULT profile_enumerate = SUCCEEDED(profiles_create)
      ? profiles->EnumLanguageProfiles(ziliu::tsf::kSimplifiedChineseLanguageId, &profile_enumerator)
      : profiles_create;
  bool exact_profile_listed = false;
  HRESULT profile_terminal = profile_enumerate;
  if (SUCCEEDED(profile_enumerate) && profile_enumerator != nullptr) {
    TF_LANGUAGEPROFILE profile{};
    ULONG fetched = 0;
    for (;;) {
      const HRESULT next = profile_enumerator->Next(1, &profile, &fetched);
      if (next != S_OK || fetched != 1) {
        profile_terminal = next;
        break;
      }
      if (IsEqualGUID(profile.clsid, ziliu::tsf::kTextServiceClsid) &&
          IsEqualGUID(profile.guidProfile, ziliu::tsf::kSimplifiedChineseProfileGuid)) {
        exact_profile_listed = true;
      }
    }
  } else if (SUCCEEDED(profile_enumerate)) {
    profile_terminal = E_UNEXPECTED;
  }
  if (profile_enumerator != nullptr) profile_enumerator->Release();
  if (profiles != nullptr) profiles->Release();

  std::wofstream output(output_path, std::ios::binary | std::ios::trunc);
  if (!output.is_open()) {
    if (uninitialize) CoUninitialize();
    *error = L"Could not write the caller-specified audit path.";
    return false;
  }
  output << L"{\n  \"testOnly\": true,\n  \"mode\": \"audit-registration\",\n"
         << L"  \"serviceClsid\": \"" << GuidText(ziliu::tsf::kTextServiceClsid) << L"\",\n"
         << L"  \"profileGuid\": \"" << GuidText(ziliu::tsf::kSimplifiedChineseProfileGuid) << L"\",\n"
         << L"  \"languageId\": \"0x0804\",\n"
         << L"  \"coInitializeHresult\": \"" << HresultText(initialize_result) << L"\",\n"
         << L"  \"categoryManagerCreateHresult\": \"" << HresultText(category_create) << L"\",\n"
         << L"  \"enumCategoriesInItemHresult\": \"" << HresultText(category_enumerate) << L"\",\n"
         << L"  \"enumCategoriesTerminalHresult\": \"" << HresultText(category_terminal)
         << L"\",\n  \"enumCategoriesComplete\": "
         << (category_terminal == S_FALSE ? L"true" : L"false") << L",\n"
         << L"  \"categories\": [";
  for (std::size_t index = 0; index < categories.size(); ++index) {
    output << L'\"' << GuidText(categories[index]) << L'\"';
    if (index + 1 != categories.size()) output << L", ";
  }
  output << L"],\n  \"inputProcessorProfilesCreateHresult\": \""
         << HresultText(profiles_create) << L"\",\n  \"enumInputProcessorInfoHresult\": \""
         << HresultText(processor_enumerate) << L"\",\n  \"serviceClsidListed\": "
         << (service_listed ? L"true" : L"false")
         << L",\n  \"enumInputProcessorInfoTerminalHresult\": \""
         << HresultText(processor_terminal) << L"\",\n  \"enumInputProcessorInfoComplete\": "
         << (processor_terminal == S_FALSE ? L"true" : L"false")
         << L",\n  \"enumLanguageProfilesHresult\": \"" << HresultText(profile_enumerate)
         << L"\",\n  \"exactLanguageProfileListed\": "
         << (exact_profile_listed ? L"true" : L"false")
         << L",\n  \"enumLanguageProfilesTerminalHresult\": \""
         << HresultText(profile_terminal) << L"\",\n  \"enumLanguageProfilesComplete\": "
         << (profile_terminal == S_FALSE ? L"true" : L"false") << L"\n}\n";
  const bool succeeded = output.good() && category_terminal == S_FALSE &&
                         processor_terminal == S_FALSE && profile_terminal == S_FALSE;
  if (uninitialize) CoUninitialize();
  if (!succeeded) {
    *error = L"Writing the caller-specified audit path failed.";
  }
  return succeeded;
}

void SetStatus(HWND status, std::wstring_view text) {
  SetWindowTextW(status, std::wstring(text).c_str());
}

HRESULT ApplyInputScope(SetInputScopeFunction set_input_scope, HWND edit, InputScope scope) {
  return set_input_scope == nullptr ? HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND)
                                    : set_input_scope(edit, scope);
}

void CreateControls(HWND window, AppState* state) {
  constexpr int kLabelX = 12;
  constexpr int kEditX = 210;
  constexpr int kStatusX = 450;
  constexpr int kFirstY = 16;
  constexpr int kRowHeight = 38;
  const HMODULE msctf_module = LoadLibraryExW(L"msctf.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
  const auto set_input_scope = msctf_module == nullptr ? nullptr
      : reinterpret_cast<SetInputScopeFunction>(GetProcAddress(msctf_module, "SetInputScope"));
  for (std::size_t index = 0; index < state->fields.size(); ++index) {
    Field& field = state->fields[index];
    const int y = kFirstY + static_cast<int>(index) * kRowHeight;
    CreateWindowExW(0, L"STATIC", std::wstring(field.label).c_str(), WS_CHILD | WS_VISIBLE,
                    kLabelX, y + 4, 190, 22, window, nullptr, nullptr, nullptr);
    field.edit = CreateWindowExW(WS_EX_CLIENTEDGE, MSFTEDIT_CLASS, L"",
                                 WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL |
                                     field.edit_style,
                                 kEditX, y, 220, 24, window, nullptr, nullptr, nullptr);
    if (field.edit != nullptr) {
      // A classic EDIT may have no focused TSF document manager even when
      // SetInputScope succeeds. Rich Edit explicitly enables its TSF text store.
      SendMessageW(field.edit, EM_SETEDITSTYLE, SES_USECTF, SES_USECTF);
    }
    if (field.scope.has_value()) {
      field.set_scope_result = ApplyInputScope(set_input_scope, field.edit, *field.scope);
      field.scope_applied = true;
    }
    const std::wstring status = field.scope_applied
        ? L"SetInputScope: " + HresultText(field.set_scope_result)
        : L"SetInputScope: NOT_APPLIED";
    CreateWindowExW(0, L"STATIC", status.c_str(), WS_CHILD | WS_VISIBLE, kStatusX, y + 4, 210,
                    22, window, nullptr, nullptr, nullptr);
  }
  CreateWindowExW(0, L"BUTTON", L"Export bounded JSON", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                  kEditX, kFirstY + static_cast<int>(state->fields.size()) * kRowHeight + 4, 170,
                  28, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kExportButtonId)), nullptr,
                  nullptr);
  state->status = CreateWindowExW(0, L"STATIC", L"No data is exported until the button is clicked.",
                                  WS_CHILD | WS_VISIBLE, kEditX + 182,
                                  kFirstY + static_cast<int>(state->fields.size()) * kRowHeight + 8,
                                  300, 22, window, nullptr, nullptr, nullptr);
  if (msctf_module != nullptr) FreeLibrary(msctf_module);
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  constexpr UINT_PTR kFocusCaptureTimer = 1;
  auto* state = reinterpret_cast<AppState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
  switch (message) {
    case WM_NCCREATE:
      SetWindowLongPtrW(window, GWLP_USERDATA,
                         reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams));
      return TRUE;
    case WM_CREATE:
      CreateControls(window, state);
      return 0;
    case WM_COMMAND:
      if (HIWORD(wparam) == EN_SETFOCUS) {
        // EN_SETFOCUS arrives before Rich Edit has published its TSF focus.
        // Read the context on the message loop after the focus transition.
        static_cast<void>(SetTimer(window, kFocusCaptureTimer, 100, nullptr));
        return 0;
      }
      if (LOWORD(wparam) == kExportButtonId && HIWORD(wparam) == BN_CLICKED) {
        std::wstring error;
        if (Export(*state, &error)) {
          SetStatus(state->status, L"Exported only this test window's fields to the caller path.");
        } else {
          SetStatus(state->status, error);
        }
      }
      return 0;
    case WM_TIMER:
      if (wparam == kFocusCaptureTimer) {
        KillTimer(window, kFocusCaptureTimer);
        CaptureFocusedContext(state, GetFocus());
        SetStatus(state->status, L"Focused Rich Edit TSF context captured; export to inspect it.");
        return 0;
      }
      return DefWindowProcW(window, message, wparam, lparam);
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
    default:
      return DefWindowProcW(window, message, wparam, lparam);
  }
}

std::optional<Arguments> ParseArguments() {
  int count = 0;
  LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &count);
  if (arguments == nullptr) return std::nullopt;
  std::optional<Arguments> parsed;
  if (count == 3 && (std::wstring_view(arguments[1]) == L"--output" ||
                     std::wstring_view(arguments[1]) == L"--audit-registration")) {
    std::filesystem::path candidate(arguments[2]);
    if (candidate.is_absolute()) {
      parsed = Arguments{.mode = std::wstring_view(arguments[1]) == L"--output"
              ? Mode::kFields : Mode::kAuditRegistration,
          .output_path = candidate};
    }
  }
  LocalFree(arguments);
  return parsed;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show_command) {
  const std::optional<Arguments> arguments = ParseArguments();
  if (!arguments.has_value()) {
    MessageBoxW(nullptr, L"Usage: ziliu_alpha_input_scope_host.exe --output|--audit-registration <absolute-json-path>",
                kWindowTitle, MB_OK | MB_ICONERROR);
    return 2;
  }
  if (arguments->mode == Mode::kAuditRegistration) {
    std::wstring error;
    return AuditRegistration(arguments->output_path, &error) ? 0 : 1;
  }
  const HRESULT initialize_result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  if (FAILED(initialize_result)) {
    MessageBoxW(nullptr, L"The TEST ONLY InputScope host requires COM initialization on its UI thread.",
                kWindowTitle, MB_OK | MB_ICONERROR);
    return 1;
  }
  AppState state{.output_path = arguments->output_path};
  state.rich_edit_module = LoadLibraryExW(L"Msftedit.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
  if (state.rich_edit_module == nullptr) {
    CoUninitialize();
    return 1;
  }
  state.thread_manager_create = CoCreateInstance(CLSID_TF_ThreadMgr, nullptr, CLSCTX_INPROC_SERVER,
                                                 IID_PPV_ARGS(&state.thread_manager));
  if (SUCCEEDED(state.thread_manager_create)) {
    state.thread_manager_activate = state.thread_manager->Activate(&state.client_id);
  }
  WNDCLASSW window_class{};
  window_class.lpfnWndProc = WindowProc;
  window_class.hInstance = instance;
  window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  window_class.lpszClassName = kWindowClass;
  if (RegisterClassW(&window_class) == 0) {
    if (SUCCEEDED(state.thread_manager_activate)) state.thread_manager->Deactivate();
    if (state.thread_manager != nullptr) state.thread_manager->Release();
    FreeLibrary(state.rich_edit_module);
    CoUninitialize();
    return 1;
  }
  HWND window = CreateWindowExW(0, kWindowClass, kWindowTitle,
                                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                                CW_USEDEFAULT, CW_USEDEFAULT, 690, 350, nullptr, nullptr, instance, &state);
  if (window == nullptr) {
    if (SUCCEEDED(state.thread_manager_activate)) state.thread_manager->Deactivate();
    if (state.thread_manager != nullptr) state.thread_manager->Release();
    FreeLibrary(state.rich_edit_module);
    CoUninitialize();
    return 1;
  }
  ShowWindow(window, show_command);
  MSG message{};
  while (GetMessageW(&message, nullptr, 0, 0) > 0) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
  if (SUCCEEDED(state.thread_manager_activate)) state.thread_manager->Deactivate();
  if (state.thread_manager != nullptr) state.thread_manager->Release();
  FreeLibrary(state.rich_edit_module);
  CoUninitialize();
  return 0;
}
