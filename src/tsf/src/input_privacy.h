#pragma once

#include <windows.h>
#include <msctf.h>
#include <wrl/client.h>

#include <atomic>
#include <new>

// inputscope.h supplies DEFINE_GUID declarations that uuid.lib does not provide.
#include <initguid.h>
#include <inputscope.h>

namespace ziliu::tsf::detail {

// InputScope is advisory, not a security boundary. Password/PIN scopes and TSF's
// disabled contexts bypass the IME. Optional or private scope metadata remains
// usable, but is marked restricted so callers can isolate state and suppress all
// learning/persistence. Unknown never means ordinary.
enum class InputPrivacy {
  kBlocked,
  kRestricted,
  kOrdinary,
};

inline bool ShouldResetInputSession(InputPrivacy previous, InputPrivacy next,
                                    bool context_changed) {
  return previous != InputPrivacy::kBlocked &&
         (context_changed || previous != next);
}

inline bool IsSecretScope(InputScope scope) {
  switch (scope) {
    case IS_PASSWORD:
    case IS_NUMERIC_PASSWORD:
    case IS_NUMERIC_PIN:
    case IS_ALPHANUMERIC_PIN:
    case IS_ALPHANUMERIC_PIN_SET:
      return true;
    default:
      return false;
  }
}

inline bool IsPrivateScope(InputScope scope) { return scope == IS_PRIVATE; }

inline bool IsKnownOrdinaryScope(InputScope scope) {
  const int value = static_cast<int>(scope);
  return value >= static_cast<int>(IS_DEFAULT) &&
         value <= static_cast<int>(IS_CHAT_WITHOUT_EMOJI) &&
         !IsSecretScope(scope) && !IsPrivateScope(scope);
}

inline bool CompartmentBlocksInput(ITfCompartmentMgr* manager, REFGUID id) {
  Microsoft::WRL::ComPtr<ITfCompartment> compartment;
  if (manager == nullptr ||
      FAILED(manager->GetCompartment(id, compartment.GetAddressOf())) || !compartment) {
    return true;
  }
  VARIANT value;
  VariantInit(&value);
  const HRESULT result = compartment->GetValue(&value);
  const bool blocked = FAILED(result) ||
      (value.vt != VT_EMPTY && (value.vt != VT_I4 || value.lVal != 0));
  VariantClear(&value);
  return blocked;
}

inline bool ContextBlocksInput(ITfContext* context, DWORD activation_flags) {
  if (context == nullptr || (activation_flags & TF_TMAE_SECUREMODE) != 0) {
    return true;
  }
  Microsoft::WRL::ComPtr<ITfCompartmentMgr> manager;
  if (FAILED(context->QueryInterface(IID_PPV_ARGS(&manager)))) {
    return true;
  }
  return CompartmentBlocksInput(manager.Get(), GUID_COMPARTMENT_KEYBOARD_DISABLED) ||
         CompartmentBlocksInput(manager.Get(), GUID_COMPARTMENT_EMPTYCONTEXT);
}

inline InputPrivacy ClassifyScopeValue(const VARIANT& value) {
  if (value.vt == VT_EMPTY) {
    return InputPrivacy::kRestricted;
  }
  if (value.vt != VT_UNKNOWN || value.punkVal == nullptr) {
    return InputPrivacy::kRestricted;
  }
  Microsoft::WRL::ComPtr<ITfInputScope> input_scope;
  if (FAILED(value.punkVal->QueryInterface(IID_PPV_ARGS(&input_scope)))) {
    return InputPrivacy::kRestricted;
  }
  InputScope* scopes = nullptr;
  UINT count = 0;
  const HRESULT result = input_scope->GetInputScopes(&scopes, &count);
  if (FAILED(result) || count == 0 || scopes == nullptr) {
    CoTaskMemFree(scopes);
    return InputPrivacy::kRestricted;
  }
  InputPrivacy privacy = InputPrivacy::kOrdinary;
  for (UINT index = 0; index < count; ++index) {
    if (IsSecretScope(scopes[index])) {
      privacy = InputPrivacy::kBlocked;
      break;
    }
    if (IsPrivateScope(scopes[index])) {
      privacy = InputPrivacy::kRestricted;
      continue;
    }
    if (!IsKnownOrdinaryScope(scopes[index])) {
      privacy = InputPrivacy::kRestricted;
    }
  }
  CoTaskMemFree(scopes);
  return privacy;
}

// Read only the scope property at the selection. Never retrieve document text.
inline InputPrivacy ClassifyScope(ITfContext* context, TfEditCookie cookie) {
  TF_SELECTION selection{};
  ULONG fetched = 0;
  const HRESULT selected = context->GetSelection(cookie, TF_DEFAULT_SELECTION, 1,
                                                  &selection, &fetched);
  Microsoft::WRL::ComPtr<ITfRange> range;
  range.Attach(selection.range);
  if (FAILED(selected) || fetched != 1 || !range) {
    return InputPrivacy::kBlocked;
  }
  if (selection.style.fInterimChar != FALSE) {
    return InputPrivacy::kBlocked;
  }
  if (selection.style.ase == TF_AE_NONE) {
    // Some hosts report no active end for an insertion caret. Accept that
    // representation only when the selection itself proves it is a caret.
    BOOL empty = FALSE;
    if (range->IsEmpty(cookie, &empty) != S_OK || empty == FALSE) {
      return InputPrivacy::kBlocked;
    }
  } else if (selection.style.ase != TF_AE_START &&
             selection.style.ase != TF_AE_END) {
    return InputPrivacy::kBlocked;
  }
  // A nonempty selection may span distinct scope values: GetValue then returns
  // S_FALSE/VT_EMPTY, which is not evidence of an ordinary insertion point.
  if (FAILED(range->Collapse(cookie, selection.style.ase == TF_AE_START
                                        ? TF_ANCHOR_START : TF_ANCHOR_END))) {
    return InputPrivacy::kBlocked;
  }
  Microsoft::WRL::ComPtr<ITfReadOnlyProperty> property;
  const HRESULT result = context->GetAppProperty(GUID_PROP_INPUTSCOPE, property.GetAddressOf());
  if (FAILED(result) || !property) {
    return InputPrivacy::kRestricted;
  }
  VARIANT value;
  VariantInit(&value);
  const HRESULT read = property->GetValue(cookie, range.Get(), &value);
  const InputPrivacy privacy = SUCCEEDED(read) ? ClassifyScopeValue(value)
                                               : InputPrivacy::kRestricted;
  VariantClear(&value);
  return privacy;
}

class InputScopeReadSession final : public ITfEditSession {
 public:
  explicit InputScopeReadSession(ITfContext* context) : context_(context) {}

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
    privacy_ = ClassifyScope(context_.Get(), cookie);
    return S_OK;
  }

  [[nodiscard]] InputPrivacy privacy() const { return privacy_; }

 private:
  std::atomic<ULONG> references_{1};
  Microsoft::WRL::ComPtr<ITfContext> context_;
  InputPrivacy privacy_ = InputPrivacy::kBlocked;
};

inline InputPrivacy ClassifyInputContext(ITfContext* context, TfClientId client_id,
                                         DWORD activation_flags) {
  if (ContextBlocksInput(context, activation_flags)) return InputPrivacy::kBlocked;
  auto* session = new (std::nothrow) InputScopeReadSession(context);
  if (session == nullptr) return InputPrivacy::kBlocked;
  HRESULT session_result = E_FAIL;
  const HRESULT requested = context->RequestEditSession(
      client_id, session, TF_ES_SYNC | TF_ES_READ, &session_result);
  const InputPrivacy privacy = requested == S_OK && session_result == S_OK
      ? session->privacy() : InputPrivacy::kBlocked;
  session->Release();
  return privacy;
}

}  // namespace ziliu::tsf::detail
