#include "ziliu/tsf/language_bar_button.h"

#include "ziliu/tsf/guids.h"
#include "ziliu/tsf/module_state.h"

#include <olectl.h>
#include <shellapi.h>

#include <array>
#include <cstdint>
#include <cwchar>
#include <string>

namespace ziliu::tsf {
namespace {

constexpr ULONGLONG kQuickMenuDebounceMilliseconds = 1500;

HICON CreateModeIcon(bool chinese_mode) {
  constexpr int size = 32;
  BITMAPV5HEADER header{};
  header.bV5Size = sizeof(header);
  header.bV5Width = size;
  header.bV5Height = -size;
  header.bV5Planes = 1;
  header.bV5BitCount = 32;
  header.bV5Compression = BI_BITFIELDS;
  header.bV5RedMask = 0x00FF0000;
  header.bV5GreenMask = 0x0000FF00;
  header.bV5BlueMask = 0x000000FF;
  header.bV5AlphaMask = 0xFF000000;

  void* pixels = nullptr;
  HDC screen = GetDC(nullptr);
  HBITMAP color_bitmap = CreateDIBSection(
      screen, reinterpret_cast<BITMAPINFO*>(&header), DIB_RGB_COLORS, &pixels, nullptr, 0);
  HDC memory = CreateCompatibleDC(screen);
  ReleaseDC(nullptr, screen);
  if (color_bitmap == nullptr || memory == nullptr || pixels == nullptr) {
    if (memory != nullptr) {
      DeleteDC(memory);
    }
    if (color_bitmap != nullptr) {
      DeleteObject(color_bitmap);
    }
    return nullptr;
  }

  const HGDIOBJ previous_bitmap = SelectObject(memory, color_bitmap);
  SetBkMode(memory, TRANSPARENT);
  SetTextColor(memory, RGB(255, 255, 255));
  HFONT font = CreateFontW(-24, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                           DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
  const HGDIOBJ previous_font = SelectObject(memory, font);
  RECT rectangle{0, -1, size, size};
  const wchar_t* glyph = chinese_mode ? L"中" : L"英";
  DrawTextW(memory, glyph, 1, &rectangle, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

  auto* pixel_values = static_cast<std::uint32_t*>(pixels);
  for (int index = 0; index < size * size; ++index) {
    if ((pixel_values[index] & 0x00FFFFFFU) != 0) {
      pixel_values[index] |= 0xFF000000U;
    }
  }

  SelectObject(memory, previous_font);
  SelectObject(memory, previous_bitmap);
  DeleteObject(font);
  DeleteDC(memory);

  HBITMAP mask_bitmap = CreateBitmap(size, size, 1, 1, nullptr);
  ICONINFO icon_info{};
  icon_info.fIcon = TRUE;
  icon_info.hbmMask = mask_bitmap;
  icon_info.hbmColor = color_bitmap;
  HICON icon = CreateIconIndirect(&icon_info);
  DeleteObject(mask_bitmap);
  DeleteObject(color_bitmap);
  return icon;
}

}  // namespace

LanguageBarButton::LanguageBarButton(std::wstring settings_executable,
                                     std::function<HRESULT()> toggle_input_mode)
    : settings_executable_(std::move(settings_executable)),
      toggle_input_mode_(std::move(toggle_input_mode)) {
  AddModuleReference();
}

LanguageBarButton::~LanguageBarButton() {
  if (sink_ != nullptr) {
    sink_->Release();
  }
  ReleaseModuleReference();
}

STDMETHODIMP LanguageBarButton::QueryInterface(REFIID interface_id, void** object) {
  if (object == nullptr) {
    return E_INVALIDARG;
  }
  *object = nullptr;
  if (IsEqualIID(interface_id, IID_IUnknown) ||
      IsEqualIID(interface_id, IID_ITfLangBarItem) ||
      IsEqualIID(interface_id, IID_ITfLangBarItemButton)) {
    *object = static_cast<ITfLangBarItemButton*>(this);
  } else if (IsEqualIID(interface_id, IID_ITfSource)) {
    *object = static_cast<ITfSource*>(this);
  }
  if (*object == nullptr) {
    return E_NOINTERFACE;
  }
  AddRef();
  return S_OK;
}

STDMETHODIMP_(ULONG) LanguageBarButton::AddRef() { return ++reference_count_; }

STDMETHODIMP_(ULONG) LanguageBarButton::Release() {
  const ULONG count = --reference_count_;
  if (count == 0) {
    delete this;
  }
  return count;
}

STDMETHODIMP LanguageBarButton::GetInfo(TF_LANGBARITEMINFO* info) {
  if (info == nullptr) {
    return E_INVALIDARG;
  }
  *info = {};
  info->clsidService = kTextServiceClsid;
  // Windows 8 and later only surface an IME mode item in the taskbar input
  // indicator when it uses this system-defined identity.
  info->guidItem = GUID_LBI_INPUTMODE;
  info->dwStyle =
      TF_LBI_STYLE_BTN_BUTTON | TF_LBI_STYLE_BTN_MENU | TF_LBI_STYLE_SHOWNINTRAY;
  info->ulSort = 0;
  constexpr wchar_t description[] = L"字流中英文状态";
  static_assert(std::size(description) <= TF_LBI_DESC_MAXLEN);
  std::wmemcpy(info->szDescription, description, std::size(description));
  return S_OK;
}

STDMETHODIMP LanguageBarButton::GetStatus(DWORD* status) {
  if (status == nullptr) {
    return E_INVALIDARG;
  }
  *status = visible_ ? 0U : TF_LBI_STATUS_HIDDEN;
  return S_OK;
}

STDMETHODIMP LanguageBarButton::Show(BOOL show) {
  visible_ = show != FALSE;
  NotifyUpdate(TF_LBI_STATUS);
  return S_OK;
}

STDMETHODIMP LanguageBarButton::GetTooltipString(BSTR* tooltip) {
  if (tooltip == nullptr) {
    return E_INVALIDARG;
  }
  *tooltip = SysAllocString(chinese_mode_ ? L"字流：中文输入（左键切换，右键菜单）"
                                         : L"字流：英文输入（左键切换，右键菜单）");
  return *tooltip != nullptr ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP LanguageBarButton::OnClick(TfLBIClick click, POINT point, const RECT* area) {
  if (click == TF_LBI_CLK_LEFT) {
    return toggle_input_mode_ ? toggle_input_mode_() : S_OK;
  }
  if (click != TF_LBI_CLK_RIGHT || settings_executable_.empty()) {
    return S_OK;
  }
  const LONG x = area != nullptr ? area->left + (area->right - area->left) / 2 : point.x;
  const LONG y = area != nullptr ? area->top : point.y;
  return ScheduleQuickMenu(x, y);
}

HRESULT LanguageBarButton::ScheduleQuickMenu(LONG x, LONG y) {
  if (settings_executable_.empty()) {
    return S_OK;
  }
  const ULONGLONG now = GetTickCount64();
  if (last_menu_request_tick_ != 0 &&
      now - last_menu_request_tick_ < kQuickMenuDebounceMilliseconds) {
    return S_OK;
  }
  last_menu_request_tick_ = now;
  return OpenQuickMenu(x, y);
}

HRESULT LanguageBarButton::ShowQuickMenu(LONG x, LONG y) {
  return ScheduleQuickMenu(x, y);
}

HRESULT LanguageBarButton::OpenQuickMenu(LONG x, LONG y) {
  if (settings_executable_.empty()) {
    return S_OK;
  }
  const std::wstring arguments =
      L"--quick-menu --x " + std::to_wstring(x) + L" --y " + std::to_wstring(y);
  const HINSTANCE result = ShellExecuteW(nullptr, L"open", settings_executable_.c_str(),
                                         arguments.c_str(), nullptr, SW_SHOWNORMAL);
  return reinterpret_cast<INT_PTR>(result) > 32 ? S_OK : E_FAIL;
}

STDMETHODIMP LanguageBarButton::InitMenu(ITfMenu* menu) {
  if (menu == nullptr) {
    return E_INVALIDARG;
  }
  POINT cursor{};
  if (!GetCursorPos(&cursor)) {
    return HRESULT_FROM_WIN32(GetLastError());
  }
  return ScheduleQuickMenu(cursor.x, cursor.y);
}

STDMETHODIMP LanguageBarButton::OnMenuSelect(UINT identifier) {
  static_cast<void>(identifier);
  return S_OK;
}

STDMETHODIMP LanguageBarButton::GetIcon(HICON* icon) {
  if (icon == nullptr) {
    return E_INVALIDARG;
  }
  *icon = CreateModeIcon(chinese_mode_);
  return *icon != nullptr ? S_OK : E_FAIL;
}

STDMETHODIMP LanguageBarButton::GetText(BSTR* text) {
  if (text == nullptr) {
    return E_INVALIDARG;
  }
  *text = SysAllocString(chinese_mode_ ? L"中" : L"英");
  return *text != nullptr ? S_OK : E_OUTOFMEMORY;
}

STDMETHODIMP LanguageBarButton::AdviseSink(REFIID interface_id, IUnknown* sink, DWORD* cookie) {
  if (sink == nullptr || cookie == nullptr || !IsEqualIID(interface_id, IID_ITfLangBarItemSink)) {
    return E_INVALIDARG;
  }
  if (sink_ != nullptr) {
    return CONNECT_E_ADVISELIMIT;
  }
  const HRESULT result = sink->QueryInterface(IID_PPV_ARGS(&sink_));
  if (FAILED(result)) {
    return result;
  }
  *cookie = 1;
  return S_OK;
}

STDMETHODIMP LanguageBarButton::UnadviseSink(DWORD cookie) {
  if (cookie != 1 || sink_ == nullptr) {
    return CONNECT_E_NOCONNECTION;
  }
  sink_->Release();
  sink_ = nullptr;
  return S_OK;
}

void LanguageBarButton::SetChineseMode(bool chinese_mode) {
  if (chinese_mode_ == chinese_mode) {
    return;
  }
  chinese_mode_ = chinese_mode;
  NotifyUpdate(TF_LBI_ICON | TF_LBI_TEXT | TF_LBI_TOOLTIP);
}

void LanguageBarButton::NotifyUpdate(DWORD flags) const {
  if (sink_ != nullptr) {
    sink_->OnUpdate(flags);
  }
}

}  // namespace ziliu::tsf
