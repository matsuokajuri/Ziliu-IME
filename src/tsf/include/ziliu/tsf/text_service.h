#pragma once

#include <msctf.h>
#include <windows.h>

#include <atomic>
#include <cstddef>
#include <memory>
#include <string>

namespace ziliu::tsf {

class CompositionEditSession;
struct TextServiceState;

class TextService final : public ITfTextInputProcessorEx,
                          public ITfKeyEventSink,
                          public ITfCompartmentEventSink,
                          public ITfThreadFocusSink {
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

  STDMETHODIMP OnChange(REFGUID guid) override;

  STDMETHODIMP OnSetThreadFocus() override;
  STDMETHODIMP OnKillThreadFocus() override;

 private:
  friend class CompositionEditSession;

  ~TextService();

  [[nodiscard]] bool EnsureSession();
  [[nodiscard]] bool ShouldHandleKey(WPARAM wparam) const;
  [[nodiscard]] bool IsInputModeSwitchKey(WPARAM wparam) const;
  HRESULT ToggleInputMode(ITfContext* context, BOOL* eaten, bool commit_pending_input);
  HRESULT HandleCandidatePage(ITfContext* context, bool next, BOOL* eaten);
  HRESULT CommitPendingInput(ITfContext* context, BOOL* eaten);
  HRESULT CommitText(ITfContext* context, std::wstring text, BOOL* eaten,
                     std::size_t caret_back = 0);
  HRESULT ApplyKeyResponse(ITfContext* context, WPARAM wparam, BOOL* eaten);
  HRESULT ApplyCompositionEdit(TfEditCookie edit_cookie, ITfContext* context);
  void AbandonSession(ITfContext* context);
  void RefreshSettings(bool force);
  HRESULT AdviseInputModeSinks();
  void UnadviseInputModeSinks();
  [[nodiscard]] bool ReadPublishedInputMode(bool* chinese_mode) const;
  void SynchronizeInputMode();
  void PublishInputMode();
  void ShowCandidateWindow();
  void ResetCompositionState();
  void StartBroker();
  void ResetRuntimeState();

  std::atomic<ULONG> reference_count_{1};
  ITfThreadMgr* thread_manager_ = nullptr;
  TfClientId client_id_ = TF_CLIENTID_NULL;
  DWORD activation_flags_ = 0;
  ITfSource* open_close_source_ = nullptr;
  ITfSource* conversion_source_ = nullptr;
  ITfSource* thread_focus_source_ = nullptr;
  DWORD open_close_cookie_ = TF_INVALID_COOKIE;
  DWORD conversion_cookie_ = TF_INVALID_COOKIE;
  DWORD thread_focus_cookie_ = TF_INVALID_COOKIE;
  std::unique_ptr<TextServiceState> state_;
};

}  // namespace ziliu::tsf
