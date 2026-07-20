#include "ziliu/ui/candidate_window.h"

#include <d2d1helper.h>
#include <dwmapi.h>

#include <algorithm>
#include <mutex>

namespace ziliu::ui {
namespace {

constexpr wchar_t kCandidateWindowClass[] = L"Ziliu.CandidateWindow.v1";
constexpr float kWindowWidth = 420.0F;
constexpr float kHorizontalPadding = 14.0F;
constexpr float kPreeditHeight = 42.0F;
constexpr float kCandidateHeight = 38.0F;
constexpr float kCornerRadius = 10.0F;

bool RegisterCandidateWindowClass() {
  static std::once_flag once;
  static bool result = false;
  std::call_once(once, [] {
    WNDCLASSEXW window_class{sizeof(window_class)};
    window_class.style = CS_HREDRAW | CS_VREDRAW;
    window_class.lpfnWndProc = CandidateWindow::WindowProcedure;
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.lpszClassName = kCandidateWindowClass;
    result = RegisterClassExW(&window_class) != 0;
  });
  return result;
}

}  // namespace

CandidateWindow::~CandidateWindow() {
  if (window_ != nullptr) {
    DestroyWindow(window_);
  }
}

bool CandidateWindow::Create(HWND owner) {
  if (window_ != nullptr) {
    return true;
  }
  if (!RegisterCandidateWindowClass()) {
    return false;
  }

  if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                               d2d_factory_.ReleaseAndGetAddressOf()))) {
    return false;
  }
  if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                 reinterpret_cast<IUnknown**>(
                                     dwrite_factory_.ReleaseAndGetAddressOf())))) {
    return false;
  }

  window_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kCandidateWindowClass, L"",
                            WS_POPUP, 0, 0, static_cast<int>(kWindowWidth), 64, owner, nullptr,
                            GetModuleHandleW(nullptr), this);
  if (window_ == nullptr) {
    return false;
  }

  const DWM_WINDOW_CORNER_PREFERENCE preference = DWMWCP_ROUND;
  DwmSetWindowAttribute(window_, DWMWA_WINDOW_CORNER_PREFERENCE, &preference,
                        sizeof(preference));
  return true;
}

void CandidateWindow::Show(const core::CompositionSnapshot& snapshot, POINT anchor) {
  if (window_ == nullptr || snapshot.empty()) {
    Hide();
    return;
  }

  snapshot_ = snapshot;
  const auto visible_count = std::max<std::size_t>(snapshot_.candidates.size(), 1);
  const int height = static_cast<int>(kHorizontalPadding * 2.0F + kPreeditHeight +
                                      kCandidateHeight * static_cast<float>(visible_count));
  SetWindowPos(window_, HWND_TOPMOST, anchor.x, anchor.y, static_cast<int>(kWindowWidth), height,
               SWP_NOACTIVATE | SWP_SHOWWINDOW);
  InvalidateRect(window_, nullptr, FALSE);
}

void CandidateWindow::Hide() {
  if (window_ != nullptr) {
    ShowWindow(window_, SW_HIDE);
  }
}

LRESULT CALLBACK CandidateWindow::WindowProcedure(HWND window, UINT message, WPARAM wparam,
                                                   LPARAM lparam) {
  CandidateWindow* self = nullptr;
  if (message == WM_NCCREATE) {
    const auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
    self = static_cast<CandidateWindow*>(create->lpCreateParams);
    self->window_ = window;
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  } else {
    self = reinterpret_cast<CandidateWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
  }

  if (self != nullptr) {
    return self->HandleMessage(message, wparam, lparam);
  }
  return DefWindowProcW(window, message, wparam, lparam);
}

LRESULT CandidateWindow::HandleMessage(UINT message, WPARAM wparam, LPARAM lparam) {
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
    case WM_NCDESTROY:
      window_ = nullptr;
      DiscardDeviceResources();
      return 0;
    default:
      return DefWindowProcW(window_, message, wparam, lparam);
  }
}

bool CandidateWindow::EnsureDeviceResources() {
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

  if (FAILED(render_target_->CreateSolidColorBrush(D2D1::ColorF(0x202124),
                                                   text_brush_.ReleaseAndGetAddressOf())) ||
      FAILED(render_target_->CreateSolidColorBrush(D2D1::ColorF(0x73767A),
                                                   muted_brush_.ReleaseAndGetAddressOf())) ||
      FAILED(render_target_->CreateSolidColorBrush(D2D1::ColorF(0xE7F0FF),
                                                   accent_brush_.ReleaseAndGetAddressOf()))) {
    DiscardDeviceResources();
    return false;
  }

  if (FAILED(dwrite_factory_->CreateTextFormat(
          L"Segoe UI Variable Text", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
          DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 18.0F, L"zh-CN",
          preedit_format_.ReleaseAndGetAddressOf())) ||
      FAILED(dwrite_factory_->CreateTextFormat(
          L"Segoe UI Variable Text", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
          DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 17.0F, L"zh-CN",
          candidate_format_.ReleaseAndGetAddressOf())) ||
      FAILED(dwrite_factory_->CreateTextFormat(
          L"Segoe UI Variable Text", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
          DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 12.0F, L"zh-CN",
          annotation_format_.ReleaseAndGetAddressOf()))) {
    DiscardDeviceResources();
    return false;
  }
  return true;
}

void CandidateWindow::Paint() {
  PAINTSTRUCT paint{};
  BeginPaint(window_, &paint);

  if (EnsureDeviceResources()) {
    render_target_->BeginDraw();
    render_target_->Clear(D2D1::ColorF(0xFAFAFA));

    render_target_->DrawTextW(
        snapshot_.preedit.c_str(), static_cast<UINT32>(snapshot_.preedit.size()),
        preedit_format_.Get(),
        D2D1::RectF(kHorizontalPadding, 10.0F, kWindowWidth - kHorizontalPadding,
                    kPreeditHeight),
        text_brush_.Get());
    render_target_->DrawLine(
        D2D1::Point2F(kHorizontalPadding, kPreeditHeight),
        D2D1::Point2F(kWindowWidth - kHorizontalPadding, kPreeditHeight), muted_brush_.Get(),
        0.5F);

    for (std::size_t index = 0; index < snapshot_.candidates.size(); ++index) {
      const float top = kHorizontalPadding + kPreeditHeight +
                        static_cast<float>(index) * kCandidateHeight;
      const D2D1_RECT_F row = D2D1::RectF(8.0F, top - 2.0F, kWindowWidth - 8.0F,
                                         top + kCandidateHeight - 4.0F);
      if (index == snapshot_.highlighted_index) {
        render_target_->FillRoundedRectangle(D2D1::RoundedRect(row, kCornerRadius, kCornerRadius),
                                             accent_brush_.Get());
      }

      const std::wstring label = std::to_wstring(index + 1) + L"  " +
                                 snapshot_.candidates[index].text;
      render_target_->DrawTextW(label.c_str(), static_cast<UINT32>(label.size()),
                                candidate_format_.Get(),
                                D2D1::RectF(kHorizontalPadding, top, 290.0F,
                                            top + kCandidateHeight),
                                text_brush_.Get());

      const auto& annotation = snapshot_.candidates[index].annotation;
      render_target_->DrawTextW(annotation.c_str(), static_cast<UINT32>(annotation.size()),
                                annotation_format_.Get(),
                                D2D1::RectF(300.0F, top + 4.0F, kWindowWidth - kHorizontalPadding,
                                            top + kCandidateHeight),
                                muted_brush_.Get());
    }

    if (render_target_->EndDraw() == D2DERR_RECREATE_TARGET) {
      DiscardDeviceResources();
    }
  }

  EndPaint(window_, &paint);
}

void CandidateWindow::DiscardDeviceResources() {
  preedit_format_.Reset();
  candidate_format_.Reset();
  annotation_format_.Reset();
  accent_brush_.Reset();
  muted_brush_.Reset();
  text_brush_.Reset();
  render_target_.Reset();
}

}  // namespace ziliu::ui
