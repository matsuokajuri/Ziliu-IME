#include "ziliu/tsf/text_service.h"

#include "ziliu/tsf/module_state.h"

namespace ziliu::tsf {

TextService::TextService() { AddModuleReference(); }

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
  }
  return advise_result;
}

STDMETHODIMP TextService::Deactivate() {
  if (thread_manager_ == nullptr) {
    return S_OK;
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

STDMETHODIMP TextService::OnSetFocus(BOOL foreground) {
  static_cast<void>(foreground);
  return S_OK;
}

STDMETHODIMP TextService::OnTestKeyDown(ITfContext* context, WPARAM wparam, LPARAM lparam,
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

STDMETHODIMP TextService::OnKeyDown(ITfContext* context, WPARAM wparam, LPARAM lparam,
                                    BOOL* eaten) {
  return OnTestKeyDown(context, wparam, lparam, eaten);
}

STDMETHODIMP TextService::OnTestKeyUp(ITfContext* context, WPARAM wparam, LPARAM lparam,
                                      BOOL* eaten) {
  return OnTestKeyDown(context, wparam, lparam, eaten);
}

STDMETHODIMP TextService::OnKeyUp(ITfContext* context, WPARAM wparam, LPARAM lparam,
                                  BOOL* eaten) {
  return OnTestKeyDown(context, wparam, lparam, eaten);
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

