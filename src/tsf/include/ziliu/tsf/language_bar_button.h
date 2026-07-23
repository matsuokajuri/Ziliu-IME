#pragma once

#include <ctffunc.h>
#include <ctfutb.h>
#include <msctf.h>
#include <windows.h>

#include <atomic>
#include <functional>
#include <string>

namespace ziliu::tsf {

class LanguageBarButton final : public ITfLangBarItemButton, public ITfSource {
 public:
  LanguageBarButton(std::wstring settings_executable, std::function<HRESULT()> toggle_input_mode);

  LanguageBarButton(const LanguageBarButton&) = delete;
  LanguageBarButton& operator=(const LanguageBarButton&) = delete;

  STDMETHODIMP QueryInterface(REFIID interface_id, void** object) override;
  STDMETHODIMP_(ULONG) AddRef() override;
  STDMETHODIMP_(ULONG) Release() override;

  STDMETHODIMP GetInfo(TF_LANGBARITEMINFO* info) override;
  STDMETHODIMP GetStatus(DWORD* status) override;
  STDMETHODIMP Show(BOOL show) override;
  STDMETHODIMP GetTooltipString(BSTR* tooltip) override;
  STDMETHODIMP OnClick(TfLBIClick click, POINT point, const RECT* area) override;
  STDMETHODIMP InitMenu(ITfMenu* menu) override;
  STDMETHODIMP OnMenuSelect(UINT identifier) override;
  STDMETHODIMP GetIcon(HICON* icon) override;
  STDMETHODIMP GetText(BSTR* text) override;

  STDMETHODIMP AdviseSink(REFIID interface_id, IUnknown* sink, DWORD* cookie) override;
  STDMETHODIMP UnadviseSink(DWORD cookie) override;

  void SetChineseMode(bool chinese_mode);

 private:
  ~LanguageBarButton();

  bool CreateQuickMenuDispatchWindow();
  HRESULT ScheduleQuickMenu(LONG x, LONG y);
  HRESULT OpenQuickMenu(LONG x, LONG y);
  static LRESULT CALLBACK QuickMenuDispatchWindowProcedure(HWND window, UINT message,
                                                           WPARAM wparam, LPARAM lparam);
  void NotifyUpdate(DWORD flags) const;

  std::atomic<ULONG> reference_count_{1};
  std::wstring settings_executable_;
  std::function<HRESULT()> toggle_input_mode_;
  HWND quick_menu_dispatch_window_ = nullptr;
  ULONGLONG last_menu_request_tick_ = 0;
  LONG pending_quick_menu_x_ = 0;
  LONG pending_quick_menu_y_ = 0;
  ITfLangBarItemSink* sink_ = nullptr;
  bool chinese_mode_ = true;
  bool visible_ = true;
};

}  // namespace ziliu::tsf
