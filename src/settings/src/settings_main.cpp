#include <d2d1.h>
#include <d2d1helper.h>
#include <dwmapi.h>
#include <dwrite.h>
#include <shellscalingapi.h>
#include <windows.h>
#include <wrl/client.h>

#include <algorithm>
#include <iterator>

namespace {

using Microsoft::WRL::ComPtr;

constexpr wchar_t kSettingsWindowClass[] = L"Ziliu.SettingsWindow.v1";
constexpr wchar_t kSettingsTitle[] = L"字流 Ziliu";

class SettingsWindow final {
 public:
  bool Create(HINSTANCE instance, int show_command) {
    WNDCLASSEXW window_class{sizeof(window_class)};
    window_class.style = CS_HREDRAW | CS_VREDRAW;
    window_class.lpfnWndProc = WindowProcedure;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.lpszClassName = kSettingsWindowClass;
    if (RegisterClassExW(&window_class) == 0) {
      return false;
    }

    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                                 d2d_factory_.ReleaseAndGetAddressOf())) ||
        FAILED(DWriteCreateFactory(
            DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(dwrite_factory_.ReleaseAndGetAddressOf())))) {
      return false;
    }

    window_ = CreateWindowExW(0, kSettingsWindowClass, kSettingsTitle, WS_OVERLAPPEDWINDOW,
                              CW_USEDEFAULT, CW_USEDEFAULT, 920, 650, nullptr, nullptr, instance,
                              this);
    if (window_ == nullptr) {
      return false;
    }

    const DWM_WINDOW_CORNER_PREFERENCE preference = DWMWCP_ROUND;
    DwmSetWindowAttribute(window_, DWMWA_WINDOW_CORNER_PREFERENCE, &preference,
                          sizeof(preference));
    ShowWindow(window_, show_command);
    UpdateWindow(window_);
    return true;
  }

  int Run() {
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
  }

 private:
  static LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wparam,
                                          LPARAM lparam) {
    SettingsWindow* self = nullptr;
    if (message == WM_NCCREATE) {
      const auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
      self = static_cast<SettingsWindow*>(create->lpCreateParams);
      self->window_ = window;
      SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
      self = reinterpret_cast<SettingsWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    }
    return self != nullptr ? self->HandleMessage(message, wparam, lparam)
                           : DefWindowProcW(window, message, wparam, lparam);
  }

  LRESULT HandleMessage(UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
      case WM_PAINT:
        Paint();
        return 0;
      case WM_SIZE:
        if (render_target_ != nullptr) {
          render_target_->Resize(D2D1::SizeU(LOWORD(lparam), HIWORD(lparam)));
        }
        return 0;
      case WM_DPICHANGED: {
        const auto* suggested = reinterpret_cast<RECT*>(lparam);
        SetWindowPos(window_, nullptr, suggested->left, suggested->top,
                     suggested->right - suggested->left, suggested->bottom - suggested->top,
                     SWP_NOACTIVATE | SWP_NOZORDER);
        return 0;
      }
      case WM_ERASEBKGND:
        return 1;
      case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
      default:
        return DefWindowProcW(window_, message, wparam, lparam);
    }
  }

  bool EnsureDeviceResources() {
    if (render_target_ != nullptr) {
      return true;
    }

    RECT client{};
    GetClientRect(window_, &client);
    const D2D1_SIZE_U size = D2D1::SizeU(static_cast<UINT32>(client.right - client.left),
                                         static_cast<UINT32>(client.bottom - client.top));
    if (FAILED(d2d_factory_->CreateHwndRenderTarget(
            D2D1::RenderTargetProperties(), D2D1::HwndRenderTargetProperties(window_, size),
            render_target_.ReleaseAndGetAddressOf()))) {
      return false;
    }

    const HRESULT brush_result = render_target_->CreateSolidColorBrush(
        D2D1::ColorF(0x1F2328), text_brush_.ReleaseAndGetAddressOf());
    const HRESULT muted_result = render_target_->CreateSolidColorBrush(
        D2D1::ColorF(0x6E7781), muted_brush_.ReleaseAndGetAddressOf());
    const HRESULT card_result = render_target_->CreateSolidColorBrush(
        D2D1::ColorF(0xFFFFFF), card_brush_.ReleaseAndGetAddressOf());
    const HRESULT accent_result = render_target_->CreateSolidColorBrush(
        D2D1::ColorF(0x1F6FEB), accent_brush_.ReleaseAndGetAddressOf());
    if (FAILED(brush_result) || FAILED(muted_result) || FAILED(card_result) ||
        FAILED(accent_result)) {
      DiscardDeviceResources();
      return false;
    }

    if (FAILED(dwrite_factory_->CreateTextFormat(
            L"Segoe UI Variable Display", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 32.0F, L"zh-CN",
            title_format_.ReleaseAndGetAddressOf())) ||
        FAILED(dwrite_factory_->CreateTextFormat(
            L"Segoe UI Variable Text", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 15.0F, L"zh-CN",
            body_format_.ReleaseAndGetAddressOf())) ||
        FAILED(dwrite_factory_->CreateTextFormat(
            L"Segoe UI Variable Text", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 18.0F, L"zh-CN",
            card_title_format_.ReleaseAndGetAddressOf()))) {
      DiscardDeviceResources();
      return false;
    }
    return true;
  }

  void Paint() {
    PAINTSTRUCT paint{};
    BeginPaint(window_, &paint);
    if (!EnsureDeviceResources()) {
      EndPaint(window_, &paint);
      return;
    }

    D2D1_SIZE_F size = render_target_->GetSize();
    render_target_->BeginDraw();
    render_target_->Clear(D2D1::ColorF(0xF6F8FA));

    constexpr wchar_t heading[] = L"字流 Ziliu";
    constexpr wchar_t subtitle[] = L"干净、安静、完全本地的中文输入体验";
    render_target_->DrawTextW(heading, static_cast<UINT32>(std::size(heading) - 1),
                              title_format_.Get(), D2D1::RectF(48, 42, size.width - 48, 92),
                              text_brush_.Get());
    render_target_->DrawTextW(subtitle, static_cast<UINT32>(std::size(subtitle) - 1),
                              body_format_.Get(), D2D1::RectF(50, 94, size.width - 48, 125),
                              muted_brush_.Get());

    const D2D1_ROUNDED_RECT status_card =
        D2D1::RoundedRect(D2D1::RectF(48, 154, size.width - 48, 274), 14, 14);
    render_target_->FillRoundedRectangle(status_card, card_brush_.Get());
    render_target_->FillRoundedRectangle(
        D2D1::RoundedRect(D2D1::RectF(68, 179, 74, 249), 3, 3), accent_brush_.Get());

    constexpr wchar_t status_title[] = L"初版骨架";
    constexpr wchar_t status_text[] =
        L"TSF 前端、候选窗、后台进程和设置中心已采用统一原生技术栈。\n"
        L"下一步将接入 librime 与雾凇拼音。";
    render_target_->DrawTextW(status_title, static_cast<UINT32>(std::size(status_title) - 1),
                              card_title_format_.Get(),
                              D2D1::RectF(94, 178, size.width - 78, 210), text_brush_.Get());
    render_target_->DrawTextW(status_text, static_cast<UINT32>(std::size(status_text) - 1),
                              body_format_.Get(), D2D1::RectF(94, 214, size.width - 78, 265),
                              muted_brush_.Get());

    constexpr wchar_t principles[] = L"现代 · 简洁 · 高效 · 低占用 · 纯粹";
    render_target_->DrawTextW(principles, static_cast<UINT32>(std::size(principles) - 1),
                              card_title_format_.Get(),
                              D2D1::RectF(48, 316, size.width - 48, 355), text_brush_.Get());

    if (render_target_->EndDraw() == D2DERR_RECREATE_TARGET) {
      DiscardDeviceResources();
    }
    EndPaint(window_, &paint);
  }

  void DiscardDeviceResources() {
    card_title_format_.Reset();
    body_format_.Reset();
    title_format_.Reset();
    accent_brush_.Reset();
    card_brush_.Reset();
    muted_brush_.Reset();
    text_brush_.Reset();
    render_target_.Reset();
  }

  HWND window_ = nullptr;
  ComPtr<ID2D1Factory> d2d_factory_;
  ComPtr<IDWriteFactory> dwrite_factory_;
  ComPtr<ID2D1HwndRenderTarget> render_target_;
  ComPtr<ID2D1SolidColorBrush> text_brush_;
  ComPtr<ID2D1SolidColorBrush> muted_brush_;
  ComPtr<ID2D1SolidColorBrush> card_brush_;
  ComPtr<ID2D1SolidColorBrush> accent_brush_;
  ComPtr<IDWriteTextFormat> title_format_;
  ComPtr<IDWriteTextFormat> body_format_;
  ComPtr<IDWriteTextFormat> card_title_format_;
};

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous_instance, wchar_t* command_line,
                    int show_command) {
  static_cast<void>(previous_instance);
  static_cast<void>(command_line);
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

  SettingsWindow window;
  if (!window.Create(instance, show_command)) {
    MessageBoxW(nullptr, L"无法启动字流设置。", kSettingsTitle, MB_OK | MB_ICONERROR);
    return 1;
  }
  return window.Run();
}
