#include "ziliu/ui/candidate_window.h"

#include <d2d1helper.h>
#include <dwmapi.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <mutex>
#include <numeric>
#include <string>
#include <string_view>

namespace ziliu::ui {
namespace {

constexpr wchar_t kCandidateWindowClass[] = L"Ziliu.CandidateWindow.v1";
constexpr float kVerticalWindowWidth = 420.0F;
constexpr float kMinimumHorizontalWindowWidth = 280.0F;
constexpr float kMinimumHorizontalCandidateWidth = 68.0F;
constexpr float kHorizontalPadding = 14.0F;
constexpr float kPreeditHeight = 42.0F;
constexpr float kCandidateHeight = 38.0F;
constexpr float kHorizontalPreeditHeight = 34.0F;
constexpr float kHorizontalCandidateHeight = 36.0F;
constexpr float kHorizontalWindowHeight = 78.0F;
constexpr float kHorizontalPreeditOnlyHeight = 42.0F;
constexpr float kCornerRadius = 10.0F;

int ToPixels(float value, float scale) {
  return static_cast<int>(std::ceil(value * scale));
}

void FitCandidateWidths(std::vector<float>* widths, float available_width, float minimum_width,
                        bool preserve_first_width) {
  if (widths == nullptr || widths->empty()) {
    return;
  }
  const float desired_width = std::accumulate(widths->begin(), widths->end(), 0.0F);
  if (desired_width <= available_width) {
    return;
  }
  if (preserve_first_width && widths->size() > 1) {
    const float other_minimum_total =
        minimum_width * static_cast<float>(widths->size() - 1);
    if (available_width > minimum_width + other_minimum_total) {
      const float first_width =
          std::min(widths->front(), available_width - other_minimum_total);
      std::vector<float> other_widths(widths->begin() + 1, widths->end());
      FitCandidateWidths(&other_widths, available_width - first_width, minimum_width, false);
      widths->front() = first_width;
      std::copy(other_widths.begin(), other_widths.end(), widths->begin() + 1);
      return;
    }
  }
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

float MeasureTextWidth(IDWriteFactory* factory, IDWriteTextFormat* format,
                       std::wstring_view text, float maximum_width, float height) {
  if (factory == nullptr || format == nullptr || text.empty() ||
      text.size() > static_cast<std::size_t>(std::numeric_limits<UINT32>::max())) {
    return 0.0F;
  }
  Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
  if (FAILED(factory->CreateTextLayout(text.data(), static_cast<UINT32>(text.size()), format,
                                       maximum_width, height, layout.GetAddressOf()))) {
    return 0.0F;
  }
  DWRITE_TEXT_METRICS metrics{};
  return SUCCEEDED(layout->GetMetrics(&metrics)) ? metrics.widthIncludingTrailingWhitespace : 0.0F;
}

bool UseDarkTheme(core::ThemeMode mode) {
  if (mode == core::ThemeMode::kDark) {
    return true;
  }
  if (mode == core::ThemeMode::kLight) {
    return false;
  }
  DWORD use_light_theme = 1;
  DWORD size = sizeof(use_light_theme);
  const LSTATUS result = RegGetValueW(
      HKEY_CURRENT_USER,
      L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
      L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &use_light_theme, &size);
  return result == ERROR_SUCCESS && use_light_theme == 0;
}

std::wstring Utf8ToWide(std::string_view value) {
  if (value.empty() ||
      value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
    return {};
  }
  const int input_length = static_cast<int>(value.size());
  const int output_length =
      MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), input_length, nullptr, 0);
  if (output_length <= 0) {
    return {};
  }
  std::wstring result(static_cast<std::size_t>(output_length), L'\0');
  if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), input_length,
                          result.data(), output_length) != output_length) {
    return {};
  }
  return result;
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

  const bool resolved_dark_theme = UseDarkTheme(settings.theme_mode);
  const bool appearance_changed =
      !dark_theme_initialized_ || dark_theme_ != resolved_dark_theme ||
      settings_.theme_mode != settings.theme_mode ||
      settings_.custom_candidate_colors != settings.custom_candidate_colors ||
      settings_.preedit_color != settings.preedit_color ||
      settings_.highlighted_candidate_color != settings.highlighted_candidate_color ||
      settings_.candidate_text_color != settings.candidate_text_color ||
      settings_.candidate_background_color != settings.candidate_background_color ||
      settings_.custom_candidate_fonts != settings.custom_candidate_fonts ||
      settings_.candidate_chinese_font_family != settings.candidate_chinese_font_family ||
      settings_.candidate_english_font_family != settings.candidate_english_font_family ||
      settings_.custom_candidate_font_size != settings.custom_candidate_font_size ||
      settings_.candidate_font_size != settings.candidate_font_size ||
      settings_.candidate_scale_with_text != settings.candidate_scale_with_text;
  snapshot_ = snapshot;
  settings_ = settings;
  dark_theme_ = resolved_dark_theme;
  dark_theme_initialized_ = true;
  const float effective_font_size = static_cast<float>(
      settings_.custom_candidate_font_size
          ? std::clamp(settings_.candidate_font_size, core::kMinimumCandidateFontSize,
                       core::kMaximumCandidateFontSize)
          : 17);
  layout_scale_ = settings_.candidate_scale_with_text
                      ? std::clamp(effective_font_size / 17.0F, 0.82F, 1.42F)
                      : 1.0F;
  if (appearance_changed) {
    DiscardDeviceResources();
  }
  const auto slice = core::MakeCandidatePageSlice(snapshot_.candidates.size(),
                                                  settings_.candidate_count, page_offset);
  page_offset_ = slice.offset;
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
  const float maximum_window_width = static_cast<float>(work_width) / dpi_scale_;
  const bool has_device_resources = EnsureDeviceResources();
  const float measured_preedit_width =
      has_device_resources
          ? MeasureTextWidth(dwrite_factory_.Get(), preedit_format_.Get(), snapshot_.preedit,
                             maximum_window_width, kPreeditHeight * layout_scale_)
          : 0.0F;
  const float desired_preedit_window_width =
      measured_preedit_width + 2.0F * kHorizontalPadding * layout_scale_;

  candidate_widths_.clear();
  candidate_lefts_.clear();
  candidate_tops_.clear();
  std::size_t horizontal_rows = slice.count == 0 ? 0 : 1;
  if (horizontal) {
    if (has_device_resources) {
      for (std::size_t visible_index = 0; visible_index < slice.count; ++visible_index) {
        const std::size_t candidate_index = slice.offset + visible_index;
        const std::wstring label = std::to_wstring(visible_index + 1) + L"  " +
                                   snapshot_.candidates[candidate_index].text;
        const float measured_width =
            MeasureTextWidth(dwrite_factory_.Get(), candidate_format_.Get(), label,
                             maximum_window_width, kHorizontalCandidateHeight * layout_scale_);
        const float width =
            std::clamp(measured_width + 24.0F * layout_scale_,
                       kMinimumHorizontalCandidateWidth * layout_scale_, maximum_window_width);
        candidate_widths_.push_back(width);
      }
    }
    if (candidate_widths_.empty() && slice.count != 0) {
      candidate_widths_.assign(slice.count,
                               kMinimumHorizontalCandidateWidth * layout_scale_);
    }
    const float outer_width = 16.0F * layout_scale_;
    const bool multiline = settings_.candidate_page_mode == core::CandidatePageMode::kMultiLine &&
                           candidate_widths_.size() > 1;
    horizontal_rows = candidate_widths_.empty() ? 0 : (multiline ? 2 : 1);
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
      FitCandidateWidths(&row_widths, maximum_window_width - outer_width,
                         kMinimumHorizontalCandidateWidth * layout_scale_, row == 0);
      std::copy(row_widths.begin(), row_widths.end(),
                candidate_widths_.begin() + static_cast<std::ptrdiff_t>(begin));
      widest_row = std::max(
          widest_row, std::accumulate(row_widths.begin(), row_widths.end(), 0.0F));
    }
    window_width_ = std::clamp(
        std::max(outer_width + widest_row, desired_preedit_window_width),
        std::min(kMinimumHorizontalWindowWidth * layout_scale_, maximum_window_width),
        maximum_window_width);
    for (std::size_t row = 0; row < horizontal_rows; ++row) {
      float left = 8.0F * layout_scale_;
      const std::size_t begin = row * columns;
      const std::size_t end = std::min(begin + columns, candidate_widths_.size());
      for (std::size_t index = begin; index < end; ++index) {
        candidate_lefts_.push_back(left);
        candidate_tops_.push_back((kHorizontalPreeditHeight + 4.0F) * layout_scale_ +
                                  static_cast<float>(row) * kHorizontalCandidateHeight *
                                      layout_scale_);
        left += candidate_widths_[index];
      }
    }
  } else {
    float desired_vertical_width =
        std::max(kVerticalWindowWidth * layout_scale_, desired_preedit_window_width);
    if (has_device_resources) {
      for (std::size_t visible_index = 0; visible_index < slice.count; ++visible_index) {
        const std::size_t candidate_index = slice.offset + visible_index;
        const std::wstring label = std::to_wstring(visible_index + 1) + L"  " +
                                   snapshot_.candidates[candidate_index].text;
        const float candidate_width =
            MeasureTextWidth(dwrite_factory_.Get(), candidate_format_.Get(), label,
                             maximum_window_width, kCandidateHeight * layout_scale_);
        desired_vertical_width =
            std::max(desired_vertical_width,
                     candidate_width + 160.0F * layout_scale_);
      }
    }
    window_width_ = std::min(desired_vertical_width, maximum_window_width);
  }

  const float height_dip =
      horizontal
          ? (horizontal_rows == 0
                 ? kHorizontalPreeditOnlyHeight * layout_scale_
                 : kHorizontalWindowHeight * layout_scale_ +
                       static_cast<float>(horizontal_rows - 1) *
                           kHorizontalCandidateHeight * layout_scale_)
          : (kHorizontalPadding * 2.0F + kPreeditHeight +
             kCandidateHeight * static_cast<float>(slice.count)) *
                layout_scale_;
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

  const core::CandidatePalette palette =
      core::ResolveCandidatePalette(settings_, dark_theme_);

  if (FAILED(render_target_->CreateSolidColorBrush(D2D1::ColorF(palette.candidate_text_color),
                                                   text_brush_.ReleaseAndGetAddressOf())) ||
      FAILED(render_target_->CreateSolidColorBrush(D2D1::ColorF(palette.preedit_color),
                                                   preedit_brush_.ReleaseAndGetAddressOf())) ||
      FAILED(render_target_->CreateSolidColorBrush(
          D2D1::ColorF(palette.highlighted_candidate_color),
          highlighted_text_brush_.ReleaseAndGetAddressOf())) ||
      FAILED(render_target_->CreateSolidColorBrush(D2D1::ColorF(palette.muted_color),
                                                   muted_brush_.ReleaseAndGetAddressOf())) ||
      FAILED(render_target_->CreateSolidColorBrush(D2D1::ColorF(palette.highlight_background_color),
                                                   accent_brush_.ReleaseAndGetAddressOf()))) {
    DiscardDeviceResources();
    return false;
  }

  std::wstring chinese_font_family = L"Source Han Sans SC";
  std::wstring english_font_family = L"Segoe UI Variable Text";
  if (settings_.custom_candidate_fonts) {
    std::wstring configured_chinese = Utf8ToWide(settings_.candidate_chinese_font_family);
    std::wstring configured_english = Utf8ToWide(settings_.candidate_english_font_family);
    if (!configured_chinese.empty()) {
      chinese_font_family = std::move(configured_chinese);
    }
    if (!configured_english.empty()) {
      english_font_family = std::move(configured_english);
    }
  }
  const float font_size = static_cast<float>(
      settings_.custom_candidate_font_size
          ? std::clamp(settings_.candidate_font_size, core::kMinimumCandidateFontSize,
                       core::kMaximumCandidateFontSize)
          : 17);
  if (FAILED(dwrite_factory_->CreateTextFormat(
          english_font_family.c_str(), nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
          DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, font_size + 1.0F, L"zh-CN",
          preedit_format_.ReleaseAndGetAddressOf())) ||
      FAILED(dwrite_factory_->CreateTextFormat(
          chinese_font_family.c_str(), nullptr, DWRITE_FONT_WEIGHT_NORMAL,
          DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, font_size, L"zh-CN",
          candidate_format_.ReleaseAndGetAddressOf())) ||
      FAILED(dwrite_factory_->CreateTextFormat(
          chinese_font_family.c_str(), nullptr, DWRITE_FONT_WEIGHT_NORMAL,
          DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
          std::max(font_size - 5.0F, 10.0F), L"zh-CN",
          annotation_format_.ReleaseAndGetAddressOf()))) {
    DiscardDeviceResources();
    return false;
  }
  render_target_->SetDpi(dpi_scale_ * static_cast<float>(USER_DEFAULT_SCREEN_DPI),
                         dpi_scale_ * static_cast<float>(USER_DEFAULT_SCREEN_DPI));
  static_cast<void>(preedit_format_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP));
  static_cast<void>(candidate_format_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP));
  static_cast<void>(candidate_format_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER));
  return true;
}

void CandidateWindow::Paint() {
  PAINTSTRUCT paint{};
  BeginPaint(window_, &paint);

  if (EnsureDeviceResources()) {
    render_target_->BeginDraw();
    const core::CandidatePalette palette =
        core::ResolveCandidatePalette(settings_, dark_theme_);
    render_target_->Clear(D2D1::ColorF(palette.candidate_background_color));

    const bool horizontal = settings_.candidate_layout == core::CandidateLayout::kHorizontal;
    const auto slice = core::MakeCandidatePageSlice(snapshot_.candidates.size(),
                                                    settings_.candidate_count, page_offset_);
    const float preedit_bottom =
        (horizontal ? kHorizontalPreeditHeight : kPreeditHeight) * layout_scale_;
    render_target_->DrawTextW(
        snapshot_.preedit.c_str(), static_cast<UINT32>(snapshot_.preedit.size()),
        preedit_format_.Get(),
        D2D1::RectF(kHorizontalPadding * layout_scale_, 10.0F * layout_scale_,
                    window_width_ - kHorizontalPadding * layout_scale_,
                    preedit_bottom),
        preedit_brush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    if (slice.count != 0) {
      render_target_->DrawLine(
          D2D1::Point2F(kHorizontalPadding * layout_scale_, preedit_bottom),
          D2D1::Point2F(window_width_ - kHorizontalPadding * layout_scale_, preedit_bottom),
          muted_brush_.Get(), 0.5F);
    }
    for (std::size_t visible_index = 0; visible_index < slice.count; ++visible_index) {
      const std::size_t candidate_index = slice.offset + visible_index;
      const float cell_width = horizontal && visible_index < candidate_widths_.size()
                                   ? candidate_widths_[visible_index]
                                   : window_width_ - 16.0F * layout_scale_;
      const float left = horizontal && visible_index < candidate_lefts_.size()
                             ? candidate_lefts_[visible_index]
                             : 8.0F * layout_scale_;
      const float top = horizontal && visible_index < candidate_tops_.size()
                            ? candidate_tops_[visible_index]
                            : (kHorizontalPadding + kPreeditHeight +
                               static_cast<float>(visible_index) * kCandidateHeight) *
                                  layout_scale_;
      const float right = horizontal ? left + cell_width - 2.0F * layout_scale_
                                     : window_width_ - 8.0F * layout_scale_;
      const float row_height =
          (horizontal ? kHorizontalCandidateHeight : kCandidateHeight) * layout_scale_;
      const D2D1_RECT_F row =
          D2D1::RectF(left, top + 1.0F, right, top + row_height - 1.0F);
      if (candidate_index == snapshot_.highlighted_index) {
        render_target_->FillRoundedRectangle(D2D1::RoundedRect(row, kCornerRadius, kCornerRadius),
                                             accent_brush_.Get());
      }

      const std::wstring label = std::to_wstring(visible_index + 1) + L"  " +
                                 snapshot_.candidates[candidate_index].text;
      const auto& annotation = snapshot_.candidates[candidate_index].annotation;
      const float vertical_annotation_left =
          annotation.empty()
              ? window_width_ - kHorizontalPadding * layout_scale_
              : std::max(300.0F * layout_scale_, window_width_ - 120.0F * layout_scale_);
      render_target_->DrawTextW(label.c_str(), static_cast<UINT32>(label.size()),
                                candidate_format_.Get(),
                                D2D1::RectF(horizontal ? left + 8.0F * layout_scale_
                                                       : kHorizontalPadding * layout_scale_,
                                            top,
                                            horizontal ? right - 6.0F * layout_scale_
                                                       : vertical_annotation_left -
                                                             10.0F * layout_scale_,
                                            top + row_height),
                                candidate_index == snapshot_.highlighted_index
                                    ? highlighted_text_brush_.Get()
                                    : text_brush_.Get(),
                                D2D1_DRAW_TEXT_OPTIONS_CLIP);

      if (!horizontal && !annotation.empty()) {
        render_target_->DrawTextW(
            annotation.c_str(), static_cast<UINT32>(annotation.size()), annotation_format_.Get(),
            D2D1::RectF(vertical_annotation_left, top + 4.0F * layout_scale_,
                        window_width_ - kHorizontalPadding * layout_scale_,
                        top + kCandidateHeight * layout_scale_),
            muted_brush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
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
  highlighted_text_brush_.Reset();
  preedit_brush_.Reset();
  text_brush_.Reset();
  render_target_.Reset();
}

}  // namespace ziliu::ui
