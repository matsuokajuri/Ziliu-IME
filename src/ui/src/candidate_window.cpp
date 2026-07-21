#include "ziliu/ui/candidate_window.h"

#include <d2d1helper.h>
#include <dwmapi.h>

#include <algorithm>
#include <cmath>
#include <mutex>
#include <numeric>
#include <string>

namespace ziliu::ui {
namespace {

constexpr wchar_t kCandidateWindowClass[] = L"Ziliu.CandidateWindow.v1";
constexpr float kVerticalWindowWidth = 420.0F;
constexpr float kMinimumHorizontalWindowWidth = 280.0F;
constexpr float kMaximumHorizontalWindowWidth = 1040.0F;
constexpr float kMinimumHorizontalCandidateWidth = 68.0F;
constexpr float kMaximumHorizontalCandidateWidth = 240.0F;
constexpr float kHorizontalPadding = 14.0F;
constexpr float kPreeditHeight = 42.0F;
constexpr float kCandidateHeight = 38.0F;
constexpr float kHorizontalPreeditHeight = 34.0F;
constexpr float kHorizontalCandidateHeight = 36.0F;
constexpr float kHorizontalWindowHeight = 78.0F;
constexpr float kCornerRadius = 10.0F;

int ToPixels(float value, float scale) {
  return static_cast<int>(std::ceil(value * scale));
}

void FitCandidateWidths(std::vector<float>* widths, float available_width) {
  if (widths == nullptr || widths->empty()) {
    return;
  }
  const float desired_width = std::accumulate(widths->begin(), widths->end(), 0.0F);
  if (desired_width <= available_width) {
    return;
  }
  const float minimum_width = kMinimumHorizontalCandidateWidth;
  const float minimum_total = minimum_width * static_cast<float>(widths->size());
  if (available_width <= minimum_total) {
    std::fill(widths->begin(), widths->end(),
              available_width / static_cast<float>(widths->size()));
    return;
  }
  const float ratio = (available_width - minimum_total) / (desired_width - minimum_total);
  for (float& width : *widths) {
    width = minimum_width + (width - minimum_width) * ratio;
  }
}

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
                            WS_POPUP, 0, 0, static_cast<int>(kVerticalWindowWidth), 64, owner,
                            nullptr,
                            GetModuleHandleW(nullptr), this);
  if (window_ == nullptr) {
    return false;
  }

  const DWM_WINDOW_CORNER_PREFERENCE preference = DWMWCP_ROUND;
  DwmSetWindowAttribute(window_, DWMWA_WINDOW_CORNER_PREFERENCE, &preference,
                        sizeof(preference));
  return true;
}

void CandidateWindow::Show(const core::CompositionSnapshot& snapshot,
                           const RECT& text_rectangle, const core::Settings& settings,
                           std::size_t page_offset) {
  if (window_ == nullptr || snapshot.empty()) {
    Hide();
    return;
  }

  snapshot_ = snapshot;
  settings_ = settings;
  const auto slice = core::MakeCandidatePageSlice(snapshot_.candidates.size(),
                                                  settings_.candidate_count, page_offset);
  page_offset_ = slice.offset;
  const auto visible_count = std::max<std::size_t>(slice.count, 1);
  const bool horizontal = settings_.candidate_layout == core::CandidateLayout::kHorizontal;
  const UINT dpi = std::max(GetDpiForWindow(window_), static_cast<UINT>(USER_DEFAULT_SCREEN_DPI));
  dpi_scale_ = static_cast<float>(dpi) / static_cast<float>(USER_DEFAULT_SCREEN_DPI);
  if (render_target_ != nullptr) {
    render_target_->SetDpi(static_cast<float>(dpi), static_cast<float>(dpi));
  }

  const POINT monitor_point{text_rectangle.left, text_rectangle.bottom};
  const HMONITOR monitor = MonitorFromPoint(monitor_point, MONITOR_DEFAULTTONEAREST);
  MONITORINFO monitor_info{sizeof(monitor_info)};
  if (!GetMonitorInfoW(monitor, &monitor_info)) {
    monitor_info.rcWork = RECT{0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
  }
  const int work_left = static_cast<int>(monitor_info.rcWork.left);
  const int work_top = static_cast<int>(monitor_info.rcWork.top);
  const int work_right = static_cast<int>(monitor_info.rcWork.right);
  const int work_bottom = static_cast<int>(monitor_info.rcWork.bottom);
  const int work_width = work_right - work_left;
  const int work_height = work_bottom - work_top;
  const float maximum_window_width = std::min(
      kMaximumHorizontalWindowWidth, static_cast<float>(work_width) / dpi_scale_);

  candidate_widths_.clear();
  if (horizontal) {
    if (EnsureDeviceResources()) {
      for (std::size_t visible_index = 0; visible_index < slice.count; ++visible_index) {
        const std::size_t candidate_index = slice.offset + visible_index;
        const std::wstring label = std::to_wstring(visible_index + 1) + L"  " +
                                   snapshot_.candidates[candidate_index].text;
        Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
        float width = kMinimumHorizontalCandidateWidth;
        if (SUCCEEDED(dwrite_factory_->CreateTextLayout(
                label.c_str(), static_cast<UINT32>(label.size()), candidate_format_.Get(),
                kMaximumHorizontalCandidateWidth, kHorizontalCandidateHeight,
                layout.GetAddressOf()))) {
          DWRITE_TEXT_METRICS metrics{};
          if (SUCCEEDED(layout->GetMetrics(&metrics))) {
            width = std::clamp(metrics.widthIncludingTrailingWhitespace + 24.0F,
                               kMinimumHorizontalCandidateWidth,
                               kMaximumHorizontalCandidateWidth);
          }
        }
        candidate_widths_.push_back(width);
      }
    }
    if (candidate_widths_.empty()) {
      candidate_widths_.assign(visible_count, kMinimumHorizontalCandidateWidth);
    }
    constexpr float outer_width = 16.0F;
    FitCandidateWidths(&candidate_widths_, maximum_window_width - outer_width);
    window_width_ = std::clamp(
        outer_width + std::accumulate(candidate_widths_.begin(), candidate_widths_.end(), 0.0F),
        std::min(kMinimumHorizontalWindowWidth, maximum_window_width), maximum_window_width);
  } else {
    window_width_ = std::min(kVerticalWindowWidth, static_cast<float>(work_width) / dpi_scale_);
  }

  const float height_dip = horizontal
                               ? kHorizontalWindowHeight
                               : kHorizontalPadding * 2.0F + kPreeditHeight +
                                     kCandidateHeight * static_cast<float>(visible_count);
  int width = ToPixels(window_width_, dpi_scale_);
  int height = ToPixels(height_dip, dpi_scale_);
  width = std::min(width, work_width);
  height = std::min(height, work_height);

  int x = text_rectangle.left;
  int y = text_rectangle.bottom + 2;
  if (x + width > work_right) {
    x = work_right - width;
  }
  x = std::max(x, work_left);
  if (y + height > work_bottom) {
    y = text_rectangle.top - height - 2;
  }
  y = std::clamp(y, work_top, work_bottom - height);

  SetWindowPos(window_, HWND_TOPMOST, x, y, width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
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
  render_target_->SetDpi(dpi_scale_ * static_cast<float>(USER_DEFAULT_SCREEN_DPI),
                         dpi_scale_ * static_cast<float>(USER_DEFAULT_SCREEN_DPI));
  static_cast<void>(candidate_format_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP));
  static_cast<void>(candidate_format_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER));
  return true;
}

void CandidateWindow::Paint() {
  PAINTSTRUCT paint{};
  BeginPaint(window_, &paint);

  if (EnsureDeviceResources()) {
    render_target_->BeginDraw();
    render_target_->Clear(D2D1::ColorF(0xFAFAFA));

    const bool horizontal = settings_.candidate_layout == core::CandidateLayout::kHorizontal;
    const float preedit_bottom = horizontal ? kHorizontalPreeditHeight : kPreeditHeight;
    render_target_->DrawTextW(
        snapshot_.preedit.c_str(), static_cast<UINT32>(snapshot_.preedit.size()),
        preedit_format_.Get(),
        D2D1::RectF(kHorizontalPadding, 10.0F, window_width_ - kHorizontalPadding,
                    preedit_bottom),
        text_brush_.Get());
    render_target_->DrawLine(
        D2D1::Point2F(kHorizontalPadding, preedit_bottom),
        D2D1::Point2F(window_width_ - kHorizontalPadding, preedit_bottom), muted_brush_.Get(),
        0.5F);

    const auto slice = core::MakeCandidatePageSlice(snapshot_.candidates.size(),
                                                    settings_.candidate_count, page_offset_);
    float horizontal_left = 8.0F;
    for (std::size_t visible_index = 0; visible_index < slice.count; ++visible_index) {
      const std::size_t candidate_index = slice.offset + visible_index;
      const float cell_width = horizontal && visible_index < candidate_widths_.size()
                                   ? candidate_widths_[visible_index]
                                   : window_width_ - 16.0F;
      const float left = horizontal ? horizontal_left : 8.0F;
      const float top = (horizontal ? kHorizontalPreeditHeight + 4.0F
                                    : kHorizontalPadding + kPreeditHeight) +
                        (horizontal ? 0.0F
                                    : static_cast<float>(visible_index) * kCandidateHeight);
      const float right = horizontal ? left + cell_width - 2.0F : window_width_ - 8.0F;
      const float row_height = horizontal ? kHorizontalCandidateHeight : kCandidateHeight;
      const D2D1_RECT_F row =
          D2D1::RectF(left, top - 2.0F, right, top + row_height - 4.0F);
      if (candidate_index == snapshot_.highlighted_index) {
        render_target_->FillRoundedRectangle(D2D1::RoundedRect(row, kCornerRadius, kCornerRadius),
                                             accent_brush_.Get());
      }

      const std::wstring label = std::to_wstring(visible_index + 1) + L"  " +
                                 snapshot_.candidates[candidate_index].text;
      render_target_->DrawTextW(label.c_str(), static_cast<UINT32>(label.size()),
                                candidate_format_.Get(),
                                D2D1::RectF(horizontal ? left + 8.0F : kHorizontalPadding, top,
                                            horizontal ? right - 6.0F : 290.0F,
                                            top + row_height),
                                text_brush_.Get());

      if (horizontal) {
        horizontal_left += cell_width;
      }

      if (!horizontal) {
        const auto& annotation = snapshot_.candidates[candidate_index].annotation;
        render_target_->DrawTextW(
            annotation.c_str(), static_cast<UINT32>(annotation.size()), annotation_format_.Get(),
            D2D1::RectF(300.0F, top + 4.0F, window_width_ - kHorizontalPadding,
                        top + kCandidateHeight),
            muted_brush_.Get());
      }
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
