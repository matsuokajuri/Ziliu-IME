#pragma once

#include <msctf.h>
#include <windows.h>

#include <atomic>
#include <memory>

namespace ziliu::tsf {

class CompositionEditSession;
struct TextServiceState;

class TextService final : public ITfTextInputProcessorEx,
                          public ITfKeyEventSink {
 public:
  TextService();

  TextService(const TextService&) = delete;
  TextService& operator=(const TextService&) = delete;

  STDMETHODIMP QueryInterface(REFIID interface_id, void** object) override;
  STDMETHODIMP_(ULONG) AddRef() override;
  STDMETHODIMP_(ULONG) Release() override;

  STDMETHODIMP Activate(ITfThreadMgr* thread_manager, TfClientId client_id) override;
  STDMETHODIMP ActivateEx(ITfThreadMgr* thread_manager, TfClientId client_id,
                          DWORD flags) override;
  STDMETHODIMP Deactivate() override;

  STDMETHODIMP OnSetFocus(BOOL foreground) override;
  STDMETHODIMP OnTestKeyDown(ITfContext* context, WPARAM wparam, LPARAM lparam,
                             BOOL* eaten) override;
  STDMETHODIMP OnKeyDown(ITfContext* context, WPARAM wparam, LPARAM lparam,
                         BOOL* eaten) override;
  STDMETHODIMP OnTestKeyUp(ITfContext* context, WPARAM wparam, LPARAM lparam,
                           BOOL* eaten) override;
  STDMETHODIMP OnKeyUp(ITfContext* context, WPARAM wparam, LPARAM lparam,
                       BOOL* eaten) override;
  STDMETHODIMP OnPreservedKey(ITfContext* context, REFGUID guid, BOOL* eaten) override;

 private:
  friend class CompositionEditSession;

  ~TextService();

  [[nodiscard]] bool EnsureSession();
  [[nodiscard]] bool ShouldHandleKey(WPARAM wparam) const;
  HRESULT ApplyKeyResponse(ITfContext* context, WPARAM wparam, BOOL* eaten);
  HRESULT ApplyCompositionEdit(TfEditCookie edit_cookie, ITfContext* context);
  void AbandonSession(ITfContext* context);
  void StartBroker();
  void ResetRuntimeState();

  std::atomic<ULONG> reference_count_{1};
  ITfThreadMgr* thread_manager_ = nullptr;
  TfClientId client_id_ = TF_CLIENTID_NULL;
  DWORD activation_flags_ = 0;
  std::unique_ptr<TextServiceState> state_;
};

}  // namespace ziliu::tsf
