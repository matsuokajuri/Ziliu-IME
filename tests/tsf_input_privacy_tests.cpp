#include "../src/tsf/src/input_privacy.h"

#include <cstdlib>
#include <iostream>
#include <string_view>
#include <vector>

namespace {
void Expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

// Stack-owned COM doubles. No TSF activation, registration, windows, broker or
// user dictionary is involved in these contract-level tests.
class Compartment final : public ITfCompartment {
 public:
  VARTYPE type = VT_EMPTY;
  LONG value = 0;
  HRESULT result = S_OK;
  STDMETHODIMP QueryInterface(REFIID id, void** out) override {
    *out = nullptr;
    if (id != IID_IUnknown && id != IID_ITfCompartment) return E_NOINTERFACE;
    *out = static_cast<ITfCompartment*>(this);
    return S_OK;
  }
  STDMETHODIMP_(ULONG) AddRef() override { return 1; }
  STDMETHODIMP_(ULONG) Release() override { return 1; }
  STDMETHODIMP SetValue(TfClientId, const VARIANT*) override { return E_NOTIMPL; }
  STDMETHODIMP GetValue(VARIANT* out) override {
    out->vt = type;
    out->lVal = value;
    return result;
  }
};

class Range final : public ITfRange {
 public:
  bool collapsed = false;
  bool empty = false;
  TfAnchor anchor = TF_ANCHOR_END;
  HRESULT collapse_result = S_OK;
  HRESULT empty_result = S_OK;
  ULONG references = 1;
  STDMETHODIMP QueryInterface(REFIID id, void** out) override {
    *out = nullptr;
    if (id != IID_IUnknown && id != IID_ITfRange) return E_NOINTERFACE;
    *out = static_cast<ITfRange*>(this);
    AddRef();
    return S_OK;
  }
  STDMETHODIMP_(ULONG) AddRef() override { return ++references; }
  STDMETHODIMP_(ULONG) Release() override { return --references; }
  STDMETHODIMP Collapse(TfEditCookie, TfAnchor selected) override {
    anchor = selected;
    collapsed = SUCCEEDED(collapse_result);
    return collapse_result;
  }
  STDMETHODIMP GetText(TfEditCookie, DWORD, WCHAR*, ULONG, ULONG*) override {
    Expect(false, "privacy query must never retrieve field text"); return E_FAIL;
  }
  STDMETHODIMP SetText(TfEditCookie, DWORD, const WCHAR*, LONG) override { return E_NOTIMPL; }
  STDMETHODIMP GetFormattedText(TfEditCookie, IDataObject**) override { return E_NOTIMPL; }
  STDMETHODIMP GetEmbedded(TfEditCookie, REFGUID, REFIID, IUnknown**) override { return E_NOTIMPL; }
  STDMETHODIMP InsertEmbedded(TfEditCookie, DWORD, IDataObject*) override { return E_NOTIMPL; }
  STDMETHODIMP ShiftStart(TfEditCookie, LONG, LONG*, const TF_HALTCOND*) override { return E_NOTIMPL; }
  STDMETHODIMP ShiftEnd(TfEditCookie, LONG, LONG*, const TF_HALTCOND*) override { return E_NOTIMPL; }
  STDMETHODIMP ShiftStartToRange(TfEditCookie, ITfRange*, TfAnchor) override { return E_NOTIMPL; }
  STDMETHODIMP ShiftEndToRange(TfEditCookie, ITfRange*, TfAnchor) override { return E_NOTIMPL; }
  STDMETHODIMP ShiftStartRegion(TfEditCookie, TfShiftDir, BOOL*) override { return E_NOTIMPL; }
  STDMETHODIMP ShiftEndRegion(TfEditCookie, TfShiftDir, BOOL*) override { return E_NOTIMPL; }
  STDMETHODIMP IsEmpty(TfEditCookie, BOOL* result) override {
    *result = empty ? TRUE : FALSE;
    return empty_result;
  }
  STDMETHODIMP IsEqualStart(TfEditCookie, ITfRange*, TfAnchor, BOOL*) override { return E_NOTIMPL; }
  STDMETHODIMP IsEqualEnd(TfEditCookie, ITfRange*, TfAnchor, BOOL*) override { return E_NOTIMPL; }
  STDMETHODIMP CompareStart(TfEditCookie, ITfRange*, TfAnchor, LONG*) override { return E_NOTIMPL; }
  STDMETHODIMP CompareEnd(TfEditCookie, ITfRange*, TfAnchor, LONG*) override { return E_NOTIMPL; }
  STDMETHODIMP AdjustForInsert(TfEditCookie, ULONG, BOOL*) override { return E_NOTIMPL; }
  STDMETHODIMP GetGravity(TfGravity*, TfGravity*) override { return E_NOTIMPL; }
  STDMETHODIMP SetGravity(TfEditCookie, TfGravity, TfGravity) override { return E_NOTIMPL; }
  STDMETHODIMP Clone(ITfRange**) override { return E_NOTIMPL; }
  STDMETHODIMP GetContext(ITfContext**) override { return E_NOTIMPL; }
};

class Property final : public ITfReadOnlyProperty {
 public:
  IUnknown* scope = nullptr;
  HRESULT result = S_OK;
  ULONG references = 1;
  STDMETHODIMP QueryInterface(REFIID id, void** out) override {
    *out = nullptr;
    if (id != IID_IUnknown && id != IID_ITfReadOnlyProperty) return E_NOINTERFACE;
    *out = static_cast<ITfReadOnlyProperty*>(this);
    AddRef();
    return S_OK;
  }
  STDMETHODIMP_(ULONG) AddRef() override { return ++references; }
  STDMETHODIMP_(ULONG) Release() override { return --references; }
  STDMETHODIMP GetType(GUID*) override { return E_NOTIMPL; }
  STDMETHODIMP EnumRanges(TfEditCookie, IEnumTfRanges**, ITfRange*) override { return E_NOTIMPL; }
  STDMETHODIMP GetValue(TfEditCookie, ITfRange* range, VARIANT* value) override {
    if (!static_cast<Range*>(range)->collapsed) {
      value->vt = VT_EMPTY;
      return S_FALSE;  // Simulate a selection containing multiple scope values.
    }
    value->vt = VT_UNKNOWN;
    value->punkVal = scope;
    if (scope != nullptr) scope->AddRef();
    return result;
  }
  STDMETHODIMP GetContext(ITfContext**) override { return E_NOTIMPL; }
};

class Context final : public ITfContext, public ITfCompartmentMgr {
 public:
  Compartment disabled;
  Compartment empty;
  HRESULT property_result = S_FALSE;
  bool compartment_failure = false;
  bool property_present = false;
  bool selection_failure = false;
  TfActiveSelEnd active_end = TF_AE_END;
  BOOL interim_character = FALSE;
  Range range;
  Property property;
  int nested_edit_requests = 0;
  HRESULT request_result = S_OK;
  STDMETHODIMP QueryInterface(REFIID id, void** out) override {
    *out = nullptr;
    if (id == IID_IUnknown || id == IID_ITfContext) *out = static_cast<ITfContext*>(this);
    if (id == IID_ITfCompartmentMgr) *out = static_cast<ITfCompartmentMgr*>(this);
    return *out != nullptr ? S_OK : E_NOINTERFACE;
  }
  STDMETHODIMP_(ULONG) AddRef() override { return 1; }
  STDMETHODIMP_(ULONG) Release() override { return 1; }
  STDMETHODIMP GetCompartment(REFGUID id, ITfCompartment** out) override {
    *out = nullptr;
    if (compartment_failure) return E_FAIL;
    if (id == GUID_COMPARTMENT_KEYBOARD_DISABLED) *out = &disabled;
    if (id == GUID_COMPARTMENT_EMPTYCONTEXT) *out = &empty;
    return *out != nullptr ? S_OK : E_INVALIDARG;
  }
  STDMETHODIMP ClearCompartment(TfClientId, REFGUID) override { return E_NOTIMPL; }
  STDMETHODIMP EnumCompartments(IEnumGUID**) override { return E_NOTIMPL; }
  STDMETHODIMP RequestEditSession(TfClientId, ITfEditSession* session, DWORD flags,
                                  HRESULT* result) override {
    ++nested_edit_requests;
    Expect(flags == (TF_ES_SYNC | TF_ES_READ), "scope read requests a synchronous read lock");
    if (request_result != S_OK) {
      *result = E_UNEXPECTED;
      return request_result;
    }
    *result = session->DoEditSession(1);
    return S_OK;
  }
  STDMETHODIMP GetAppProperty(REFGUID id, ITfReadOnlyProperty** out) override {
    Expect(id == GUID_PROP_INPUTSCOPE, "only read input-scope metadata");
    *out = nullptr;
    if (property_present) { *out = &property; property.AddRef(); }
    return property_result;
  }
  STDMETHODIMP InWriteSession(TfClientId, BOOL*) override { return E_NOTIMPL; }
  STDMETHODIMP GetSelection(TfEditCookie, ULONG, ULONG, TF_SELECTION* selection, ULONG* count) override {
    *count = 0;
    if (selection_failure) return E_FAIL;
    selection->range = &range;
    selection->style.ase = active_end;
    selection->style.fInterimChar = interim_character;
    range.AddRef();
    *count = 1;
    return S_OK;
  }
  STDMETHODIMP SetSelection(TfEditCookie, ULONG, const TF_SELECTION*) override { return E_NOTIMPL; }
  STDMETHODIMP GetStart(TfEditCookie, ITfRange**) override { return E_NOTIMPL; }
  STDMETHODIMP GetEnd(TfEditCookie, ITfRange**) override { return E_NOTIMPL; }
  STDMETHODIMP GetActiveView(ITfContextView**) override { return E_NOTIMPL; }
  STDMETHODIMP EnumViews(IEnumTfContextViews**) override { return E_NOTIMPL; }
  STDMETHODIMP GetStatus(TF_STATUS*) override { return E_NOTIMPL; }
  STDMETHODIMP GetProperty(REFGUID, ITfProperty**) override { return E_NOTIMPL; }
  STDMETHODIMP TrackProperties(const GUID**, ULONG, const GUID**, ULONG,
                               ITfReadOnlyProperty**) override { return E_NOTIMPL; }
  STDMETHODIMP EnumProperties(IEnumTfProperties**) override { return E_NOTIMPL; }
  STDMETHODIMP GetDocumentMgr(ITfDocumentMgr**) override { return E_NOTIMPL; }
  STDMETHODIMP CreateRangeBackup(TfEditCookie, ITfRange*, ITfRangeBackup**) override { return E_NOTIMPL; }
};

class Scope final : public ITfInputScope {
 public:
  std::vector<InputScope> values;
  HRESULT result = S_OK;
  bool malformed = false;
  ULONG references = 1;
  STDMETHODIMP QueryInterface(REFIID id, void** out) override {
    *out = nullptr;
    if (id != IID_IUnknown && id != __uuidof(ITfInputScope)) return E_NOINTERFACE;
    *out = static_cast<ITfInputScope*>(this);
    AddRef();
    return S_OK;
  }
  STDMETHODIMP_(ULONG) AddRef() override { return ++references; }
  STDMETHODIMP_(ULONG) Release() override { return --references; }
  STDMETHODIMP GetInputScopes(InputScope** scopes, UINT* count) override {
    *scopes = nullptr;
    *count = static_cast<UINT>(values.size());
    if (!values.empty() && !malformed) {
      *scopes = static_cast<InputScope*>(CoTaskMemAlloc(values.size() * sizeof(InputScope)));
      if (*scopes == nullptr) return E_OUTOFMEMORY;
      for (std::size_t i = 0; i < values.size(); ++i) (*scopes)[i] = values[i];
    }
    return result;
  }
  STDMETHODIMP GetPhrase(BSTR**, UINT*) override { return E_NOTIMPL; }
  STDMETHODIMP GetRegularExpression(BSTR*) override { return E_NOTIMPL; }
  STDMETHODIMP GetSRGS(BSTR*) override { return E_NOTIMPL; }
  STDMETHODIMP GetXML(BSTR*) override { return E_NOTIMPL; }
};
}

int main() {
  using namespace ziliu::tsf::detail;
  Context context;
  const auto privacy = [&context](DWORD flags = 0) {
    return ClassifyInputContext(&context, 7, flags);
  };
  Expect(ClassifyInputContext(nullptr, 7, 0) == InputPrivacy::kBlocked,
         "null context bypasses input method");
  Expect(privacy(TF_TMAE_SECUREMODE) == InputPrivacy::kBlocked,
         "secure activation bypasses engine");
  Expect(context.nested_edit_requests == 0,
          "blocked contexts never request a scope read session");
  Expect(privacy() == InputPrivacy::kRestricted,
         "absent optional scope permits restricted local composition");
  context.disabled.type = VT_I4;
  context.disabled.value = 1;
  Expect(privacy() == InputPrivacy::kBlocked, "disabled context blocks test routing");
  Expect(context.nested_edit_requests == 1,
          "disabled context does not request an edit session");
  context.disabled.value = 0;
  context.empty.type = VT_I4;
  context.empty.value = 1;
  Expect(privacy() == InputPrivacy::kBlocked, "empty context blocks test routing");
  context.empty.value = 0;
  Expect(privacy() == InputPrivacy::kRestricted,
         "return to an unknown-scope context re-enables restricted routing");
  context.disabled.result = E_FAIL;
  Expect(privacy() == InputPrivacy::kBlocked, "failed compartment read is not permission");
  context.disabled.result = S_OK;
  context.disabled.type = VT_UI4;
  Expect(privacy() == InputPrivacy::kBlocked, "unexpected compartment type is rejected");
  context.disabled.type = VT_EMPTY;
  context.compartment_failure = true;
  Expect(privacy() == InputPrivacy::kBlocked, "missing compartment interface fails closed");
  context.compartment_failure = false;
  context.property_result = E_NOTIMPL;
  Expect(privacy() == InputPrivacy::kRestricted,
         "legacy host without input scope composes without learning");
  for (HRESULT failure : {E_FAIL, TF_E_DISCONNECTED, E_OUTOFMEMORY, S_OK}) {
    context.property_result = failure;
    Expect(privacy() == InputPrivacy::kRestricted,
           "failed or absent optional scope remains restricted rather than blocked");
  }

  VARIANT value;
  VariantInit(&value);
  Expect(ClassifyScopeValue(value) == InputPrivacy::kRestricted,
         "unset input scope is restricted rather than assumed safe");
  value.vt = VT_I4;
  Expect(ClassifyScopeValue(value) == InputPrivacy::kRestricted,
         "invalid scope property type is restricted");
  value.vt = VT_UNKNOWN;
  value.punkVal = nullptr;
  Expect(ClassifyScopeValue(value) == InputPrivacy::kRestricted,
         "null scope object is restricted");
  Scope scope;
  value.punkVal = &scope;
  for (InputScope ordinary : {IS_DEFAULT, IS_TEXT, IS_SEARCH, IS_CHAT, IS_NUMBER}) {
    scope.values = {ordinary};
    Expect(ClassifyScopeValue(value) == InputPrivacy::kOrdinary,
           "ordinary scopes retain learning-capable input-method support");
  }
  scope.values = {static_cast<InputScope>(9999)};
  Expect(ClassifyScopeValue(value) == InputPrivacy::kRestricted,
         "unknown numeric scope is not assumed ordinary");
  scope.values = {IS_DEFAULT, IS_PRIVATE, IS_TEXT};
  Expect(ClassifyScopeValue(value) == InputPrivacy::kRestricted,
         "private scope permits only restricted local composition");
  for (InputScope sensitive : {IS_PASSWORD, IS_NUMERIC_PASSWORD, IS_NUMERIC_PIN,
                              IS_ALPHANUMERIC_PIN, IS_ALPHANUMERIC_PIN_SET}) {
    scope.values = {IS_DEFAULT, sensitive, IS_TEXT};
    Expect(ClassifyScopeValue(value) == InputPrivacy::kBlocked,
           "any password or PIN scope blocks the entire key");
  }
  scope.values = {IS_TEXT};
  scope.result = E_FAIL;
  Expect(ClassifyScopeValue(value) == InputPrivacy::kRestricted,
         "scope enumeration failure stays restricted");
  scope.result = S_OK;
  scope.malformed = true;
  Expect(ClassifyScopeValue(value) == InputPrivacy::kRestricted,
         "null array with nonzero count stays restricted");

  scope.malformed = false;
  context.property_result = S_OK;
  context.property_present = true;
  context.property.scope = &scope;
  for (TfActiveSelEnd end : {TF_AE_START, TF_AE_END}) {
    context.active_end = end;
    context.range.collapsed = false;
    scope.values = {IS_PRIVATE};
    Expect(privacy() == InputPrivacy::kRestricted,
           "mixed selection checks private insertion-end scope in the actual edit");
    Expect(context.range.collapsed, "range is collapsed before scope GetValue");
    Expect(context.range.anchor == (end == TF_AE_START ? TF_ANCHOR_START : TF_ANCHOR_END),
           "query the correct active end of forward/backward selection");
    scope.values = {IS_TEXT};
    Expect(privacy() == InputPrivacy::kOrdinary,
            "real property/range path accepts ordinary text");
  }
  context.range.collapse_result = E_FAIL;
  Expect(privacy() == InputPrivacy::kBlocked, "failed range collapse rejects key");
  context.range.collapse_result = S_OK;
  context.interim_character = TRUE;
  Expect(privacy() == InputPrivacy::kBlocked,
         "interim-character range has no safe insertion point");
  context.interim_character = FALSE;
  context.active_end = TF_AE_NONE;
  Expect(privacy() == InputPrivacy::kBlocked,
         "no active selection end rejects a nonempty selection");
  context.range.empty = true;
  context.range.collapsed = false;
  scope.values = {IS_SEARCH};
  Expect(privacy() == InputPrivacy::kOrdinary,
         "collapsed search caret without active end reads its ordinary scope");
  Expect(context.range.collapsed && context.range.anchor == TF_ANCHOR_END,
         "collapsed selection queries the caret location");
  scope.values = {IS_NUMERIC_PIN};
  Expect(privacy() == InputPrivacy::kBlocked,
         "collapsed no-active-end selection never bypasses a PIN scope");
  scope.values = {IS_PRIVATE};
  Expect(privacy() == InputPrivacy::kRestricted,
         "collapsed no-active-end selection retains private restrictions");
  context.range.empty_result = E_FAIL;
  Expect(privacy() == InputPrivacy::kBlocked,
         "unverifiable no-active-end selection fails closed");
  context.range.empty_result = S_OK;
  context.range.empty = false;
  context.active_end = static_cast<TfActiveSelEnd>(99);
  Expect(privacy() == InputPrivacy::kBlocked, "unknown active selection end rejects key");
  context.active_end = TF_AE_END;
  context.selection_failure = true;
  Expect(privacy() == InputPrivacy::kBlocked, "unavailable selection rejects key");
  context.selection_failure = false;
  context.property.result = E_FAIL;
  Expect(privacy() == InputPrivacy::kRestricted,
         "GetValue failure permits only restricted composition and releases its variant");
  Expect(ShouldResetInputSession(InputPrivacy::kRestricted, InputPrivacy::kOrdinary, false),
         "private-to-normal transition resets the prior composition");
  Expect(ShouldResetInputSession(InputPrivacy::kOrdinary, InputPrivacy::kRestricted, false),
         "normal-to-unknown transition resets the prior composition");
  Expect(ShouldResetInputSession(InputPrivacy::kRestricted, InputPrivacy::kRestricted, true),
         "focus context change resets a restricted composition");
  Expect(!ShouldResetInputSession(InputPrivacy::kRestricted, InputPrivacy::kRestricted, false),
         "stable restricted context keeps its current composition");
  Expect(context.range.references == 1 && context.property.references == 1 && scope.references == 1,
         "range, property and input scope references are balanced across allowed/denied/error paths");
  std::cout << "TSF privacy contracts PASS (not a real-application password-field gate)\n";
}
