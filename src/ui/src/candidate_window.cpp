#include "ziliu/ui/candidate_window.h"

#include <d2d1helper.h>
#include <dwmapi.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
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

  const bool appearance_changed =
      settings_.theme_mode != settings.theme_mode ||
      settings_.candidate_color_scheme != settings.candidate_color_scheme ||
      settings_.candidate_font_family != settings.candidate_font_family ||
      settings_.candidate_font_size != settings.candidate_font_size;
  snapshot_ = snapshot;
  settings_ = settings;
  if (appearance_changed) {
    DiscardDeviceResources();
  }
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
  candidate_lefts_.clear();
  candidate_tops_.clear();
  std::size_t horizontal_rows = 1;
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
    const bool multiline = settings_.candidate_page_mode == core::CandidatePageMode::kMultiLine &&
                           candidate_widths_.size() > 1;
    horizontal_rows = multiline ? 2 : 1;
    const std::size_t columns =
        multiline ? (candidate_widths_.size() + 1) / 2 : candidate_widths_.size();
    float widest_row = 0.0F;
    for (std::size_t row = 0; row < horizontal_rows; ++row) {
      const std::size_t begin = row * columns;
      const std::size_t end = std::min(begin + columns, candidate_widths_.size());
      if (begin >= end) {
        continue;
      }
      std::vector<float> row_widths(candidate_widths_.begin() + static_cast<std::ptrdiff_t>(begin),
                                    candidate_widths_.begin() + static_cast<std::ptrdiff_t>(end));
      FitCandidateWidths(&row_widths, maximum_window_width - outer_width);
      std::copy(row_widths.begin(), row_widths.end(),
                candidate_widths_.begin() + static_cast<std::ptrdiff_t>(begin));
      widest_row = std::max(
          widest_row, std::accumulate(row_widths.begin(), row_widths.end(), 0.0F));
    }
    window_width_ = std::clamp(outer_width + widest_row,
                               std::min(kMinimumHorizontalWindowWidth, maximum_window_width),
                               maximum_window_width);
    for (std::size_t row = 0; row < horizontal_rows; ++row) {
      float left = 8.0F;
      const std::size_t begin = row * columns;
      const std::size_t end = std::min(begin + columns, candidate_widths_.size());
      for (std::size_t index = begin; index < end; ++index) {
        candidate_lefts_.push_back(left);
        candidate_tops_.push_back(kHorizontalPreeditHeight + 4.0F +
                                  static_cast<float>(row) * kHorizontalCandidateHeight);
        left += candidate_widths_[index];
      }
    }
  } else {
    window_width_ = std::min(kVerticalWindowWidth, static_cast<float>(work_width) / dpi_scale_);
  }

  const float height_dip = horizontal
                               ? kHorizontalWindowHeight +
                                     static_cast<float>(horizontal_rows - 1) *
                                         kHorizontalCandidateHeight
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

  std::uint32_t text_color = 0x202124;
  std::uint32_t muted_color = 0x73767A;
  std::uint32_t accent_color = 0xE7F0FF;
  if (settings_.theme_mode == core::ThemeMode::kDark) {
    text_color = 0xF5F6F7;
    muted_color = 0xAEB4BC;
    accent_color = 0x36445A;
  }
  if (settings_.candidate_color_scheme == core::CandidateColorScheme::kBlue) {
    accent_color = settings_.theme_mode == core::ThemeMode::kDark ? 0x294A78 : 0xDCEBFF;
  } else if (settings_.candidate_color_scheme == core::CandidateColorScheme::kGraphite) {
    accent_color = settings_.theme_mode == core::ThemeMode::kDark ? 0x474747 : 0xE2E2E2;
  }

  if (FAILED(render_target_->CreateSolidColorBrush(D2D1::ColorF(text_color),
                                                   text_brush_.ReleaseAndGetAddressOf())) ||
      FAILED(render_target_->CreateSolidColorBrush(D2D1::ColorF(muted_color),
                                                   muted_brush_.ReleaseAndGetAddressOf())) ||
      FAILED(render_target_->CreateSolidColorBrush(D2D1::ColorF(accent_color),
                                                   accent_brush_.ReleaseAndGetAddressOf()))) {
    DiscardDeviceResources();
    return false;
  }

  const wchar_t* font_family = L"Source Han Sans SC";
  if (settings_.candidate_font_family == core::CandidateFontFamily::kMicrosoftYaHei) {
    font_family = L"Microsoft YaHei UI";
  } else if (settings_.candidate_font_family == core::CandidateFontFamily::kSystem) {
    font_family = L"Segoe UI Variable Text";
  }
  const float font_size = static_cast<float>(std::clamp(
      settings_.candidate_font_size, core::kMinimumCandidateFontSize,
      core::kMaximumCandidateFontSize));
  if (FAILED(dwrite_factory_->CreateTextFormat(
          font_family, nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
          DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, font_size + 1.0F, L"zh-CN",
          preedit_format_.ReleaseAndGetAddressOf())) ||
      FAILED(dwrite_factory_->CreateTextFormat(
          font_family, nullptr, DWRITE_FONT_WEIGHT_NORMAL,
          DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, font_size, L"zh-CN",
          candidate_format_.ReleaseAndGetAddressOf())) ||
      FAILED(dwrite_factory_->CreateTextFormat(
          font_family, nullptr, DWRITE_FONT_WEIGHT_NORMAL,
          DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
          std::max(font_size - 5.0F, 10.0F), L"zh-CN",
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
    std::uint32_t background_color = settings_.theme_mode == core::ThemeMode::kDark
                                         ? 0x202124
                                         : 0xFAFAFA;
    if (settings_.candidate_color_scheme == core::CandidateColorScheme::kBlue) {
      background_color = settings_.theme_mode == core::ThemeMode::kDark ? 0x172033 : 0xF7FAFF;
    } else if (settings_.candidate_color_scheme == core::CandidateColorScheme::kGraphite) {
      background_color = settings_.theme_mode == core::ThemeMode::kDark ? 0x242424 : 0xF4F4F4;
    }
    render_target_->Clear(D2D1::ColorF(background_color));

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
    for (std::size_t visible_index = 0; visible_index < slice.count; ++visible_index) {
      const std::size_t candidate_index = slice.offset + visible_index;
      const float cell_width = horizontal && visible_index < candidate_widths_.size()
                                   ? candidate_widths_[visible_index]
                                   : window_width_ - 16.0F;
      const float left = horizontal && visible_index < candidate_lefts_.size()
                             ? candidate_lefts_[visible_index]
                             : 8.0F;
      const float top = horizontal && visible_index < candidate_tops_.size()
                            ? candidate_tops_[visible_index]
                            : kHorizontalPadding + kPreeditHeight +
                                  static_cast<float>(visible_index) * kCandidateHeight;
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
