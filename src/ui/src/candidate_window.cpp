#include "ziliu/ui/candidate_window.h"
#include "native_candidate_geometry.h"
#include "sogou_bitmap_patch.h"
#include "sogou_gdi_raster_scale.h"
#include "sogou_horizontal_layout.h"

#include "sogou_overlay_layout.h"
#include "ziliu/core/theme_catalog.h"

#include <d2d1helper.h>
#include <dwmapi.h>
#include <windowsx.h>
#include <UIAutomation.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <mutex>
#include <numeric>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace ziliu::ui {

struct CandidateCaretMailbox {
  std::mutex mutex;
  HWND window = nullptr;
  bool alive = true;
  bool pending = false;
  std::uint64_t revision = 0;
  std::uint64_t completed_revision = 0;
  std::optional<RECT> rectangle;
};

namespace {

constexpr UINT kCaretPositionReady = WM_APP + 0x271;
constexpr UINT_PTR kCaretPositionTimer = 0x5A03;

std::optional<RECT> ReadFocusedCaret(HWND foreground, HWND focus) {
  using Microsoft::WRL::ComPtr;
  DWORD process = 0;
  const DWORD thread = GetWindowThreadProcessId(foreground, &process);
  GUITHREADINFO info{sizeof(info)};
  if (GetForegroundWindow() != foreground || process != GetCurrentProcessId() ||
      !GetGUIThreadInfo(thread, &info) || info.hwndFocus != focus) {
    return std::nullopt;
  }
  ComPtr<IUIAutomation2> automation;
  // Windows rejects the earlier 30-ms setting; 100 ms is a supported bound.
  if (FAILED(CoCreateInstance(__uuidof(CUIAutomation8), nullptr, CLSCTX_INPROC_SERVER,
                               IID_PPV_ARGS(automation.GetAddressOf()))) ||
      FAILED(automation->put_ConnectionTimeout(100)) ||
      FAILED(automation->put_TransactionTimeout(100))) {
    return std::nullopt;
  }
  static_cast<void>(automation->put_AutoSetFocus(FALSE));
  ComPtr<IUIAutomationElement> element;
  int element_process = 0;
  if (FAILED(automation->GetFocusedElement(element.GetAddressOf())) || !element ||
      FAILED(element->get_CurrentProcessId(&element_process)) ||
      static_cast<DWORD>(element_process) != process) {
    return std::nullopt;
  }
  ComPtr<IUIAutomationTextPattern2> pattern;
  ComPtr<IUIAutomationTextRange> range;
  BOOL active = FALSE;
  if (FAILED(element->GetCurrentPatternAs(UIA_TextPattern2Id,
          IID_PPV_ARGS(pattern.GetAddressOf()))) || !pattern ||
      FAILED(pattern->GetCaretRange(&active, range.GetAddressOf())) || !active || !range) {
    return std::nullopt;
  }
  SAFEARRAY* bounds = nullptr;
  if (FAILED(range->GetBoundingRectangles(&bounds)) || !bounds) {
    return std::nullopt;
  }
  LONG first = 0;
  LONG last = -1;
  double* values = nullptr;
  std::optional<RECT> result;
  if (SafeArrayGetDim(bounds) == 1 && SafeArrayGetElemsize(bounds) == sizeof(double) &&
      SUCCEEDED(SafeArrayGetLBound(bounds, 1, &first)) &&
      SUCCEEDED(SafeArrayGetUBound(bounds, 1, &last)) && last - first + 1 == 4 &&
      SUCCEEDED(SafeArrayAccessData(bounds, reinterpret_cast<void**>(&values)))) {
    if (std::all_of(values, values + 4, [](double v) { return std::isfinite(v) && std::abs(v) < 1000000; }) &&
        values[2] >= 0 && values[3] > 0) {
      result = RECT{static_cast<LONG>(std::floor(values[0])),
                    static_cast<LONG>(std::floor(values[1])),
                    static_cast<LONG>(std::ceil(values[0] + values[2])),
                    static_cast<LONG>(std::ceil(values[1] + values[3]))};
    }
    SafeArrayUnaccessData(bounds);
  }
  SafeArrayDestroy(bounds);
  ComPtr<IUIAutomationElement> current;
  BOOL same = FALSE;
  if (GetForegroundWindow() != foreground || !GetGUIThreadInfo(thread, &info) ||
      info.hwndFocus != focus ||
      FAILED(automation->GetFocusedElement(current.GetAddressOf())) || !current ||
      FAILED(automation->CompareElements(element.Get(), current.Get(), &same)) || !same) {
    return std::nullopt;
  }
  return result;
}

struct CaretQuery {
  std::shared_ptr<CandidateCaretMailbox> mailbox;
  std::uint64_t revision;
  HMODULE module;
  HWND foreground;
  HWND focus;
};

void CALLBACK QueryCaretOnWorker(PTP_CALLBACK_INSTANCE callback, void* parameter) {
  std::unique_ptr<CaretQuery> query(static_cast<CaretQuery*>(parameter));
  // UI Automation can call back into the app's UI thread. Never block that
  // thread waiting for it, and keep this DLL mapped until the callback returns.
  FreeLibraryWhenCallbackReturns(callback, query->module);
  std::optional<RECT> rectangle;
  const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  if (SUCCEEDED(initialized)) {
    rectangle = ReadFocusedCaret(query->foreground, query->focus);
    CoUninitialize();
  }
  auto& box = *query->mailbox;
  std::lock_guard lock(box.mutex);
  box.pending = false;
  box.completed_revision = query->revision;
  box.rectangle = rectangle;
  if (box.alive && box.window) {
    PostMessageW(box.window, kCaretPositionReady,
                 reinterpret_cast<WPARAM>(query->mailbox.get()), 0);
  }
}

constexpr wchar_t kCandidateWindowClass[] = L"Ziliu.CandidateWindow.v1";
constexpr wchar_t kCandidatePreviewClass[] = L"Ziliu.CandidatePreview.v1";
constexpr UINT_PTR kNativeFadeTimer = 0x5A01;
constexpr UINT_PTR kNativeWidthTimer = 0x5A02;
// A 16ms USER timer can land on every other 15.6ms system tick (~32 FPS).
// Request the supported 10ms minimum, without changing system timer resolution.
constexpr UINT kNativeWidthInterval = USER_TIMER_MINIMUM;
constexpr float kNativeWidthDuration = 180.0F;

double NativeAnimationMilliseconds() {
  static const double ticks_per_ms = [] {
    LARGE_INTEGER frequency{};
    QueryPerformanceFrequency(&frequency);
    return static_cast<double>(frequency.QuadPart) / 1000.0;
  }();
  LARGE_INTEGER counter{};
  QueryPerformanceCounter(&counter);
  return static_cast<double>(counter.QuadPart) / ticks_per_ms;
}
constexpr float kVerticalWindowWidth = 420.0F;
constexpr float kMinimumHorizontalWindowWidth = 280.0F;
constexpr float kMinimumHorizontalCandidateWidth = 68.0F;
constexpr float kHorizontalPadding = 14.0F;
constexpr float kPreeditHeight = 42.0F;
constexpr float kCandidateHeight = 38.0F;
constexpr float kHorizontalPreeditHeight = 34.0F;
constexpr float kHorizontalCandidateHeight = 36.0F;
constexpr float kHorizontalPreeditOnlyHeight = 42.0F;
constexpr float kHorizontalExpandButtonWidth = 40.0F;
constexpr float kHorizontalMenuButtonWidth = 48.0F;
constexpr float kCornerRadius = 10.0F;
constexpr float kSurfaceCornerRadius = 8.0F;

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

void FillCandidateRoundedRect(ID2D1Factory* factory, ID2D1RenderTarget* target,
                              const D2D1_RECT_F& bounds, float radius,
                              ID2D1Brush* brush, bool cubic) {
  if (cubic) {
    const auto geometry = detail::CreateNativeRoundedGeometry(factory, bounds, radius);
    if (geometry != nullptr) {
      target->FillGeometry(geometry.Get(), brush);
      return;
    }
  }
  target->FillRoundedRectangle(D2D1::RoundedRect(bounds, radius, radius), brush);
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

std::optional<std::filesystem::path> ThemesDirectoryPath() {
  std::wstring local_app_data(32768, L'\0');
  const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", local_app_data.data(),
                                               static_cast<DWORD>(local_app_data.size()));
  if (length == 0 || static_cast<std::size_t>(length) >= local_app_data.size()) {
    return std::nullopt;
  }
  local_app_data.resize(length);
  return std::filesystem::path(local_app_data) / L"Ziliu" / L"Themes";
}

D2D1_COLOR_F ArgbColor(std::uint32_t argb) {
  constexpr float kColorScale = 1.0F / 255.0F;
  return D2D1::ColorF(static_cast<float>((argb >> 16U) & 0xFFU) * kColorScale,
                     static_cast<float>((argb >> 8U) & 0xFFU) * kColorScale,
                     static_cast<float>(argb & 0xFFU) * kColorScale,
                     static_cast<float>((argb >> 24U) & 0xFFU) * kColorScale);
}

D2D1_COLOR_F RgbColor(std::uint32_t rgb) {
  return ArgbColor(0xFF000000U | (rgb & 0x00FFFFFFU));
}

bool ContainsPoint(const D2D1_RECT_F& bounds, float x, float y) {
  return x >= bounds.left && x < bounds.right && y >= bounds.top && y < bounds.bottom;
}

D2D1_RECT_F DecodeSogouOverlayBounds(const core::ThemeOverlay& overlay,
                                     const D2D1_SIZE_F& bitmap_size,
                                     const D2D1_SIZE_F& surface_size,
                                     float destination_scale) {
  const auto bounds = detail::ResolveSogouOverlayBounds(
      overlay.align, surface_size.width, surface_size.height, bitmap_size.width,
      bitmap_size.height, destination_scale);
  return D2D1::RectF(bounds.left, bounds.top, bounds.right, bounds.bottom);
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

// Bounded GDI mask path for explicit custom-SSF text rendering. All DCs and
// bitmaps are memory-only; nothing is drawn on the user's desktop.
bool DrawSsfGdiText(ID2D1RenderTarget* target, IDWriteTextFormat* format,
                    std::wstring_view text, float font_size,
                    const D2D1_RECT_F& clip, const D2D1_COLOR_F& color,
                    std::wstring_view prefix = {}, float* measured_width = nullptr,
                    bool ink_advance = false, float* measured_height = nullptr,
                    float measure_dpi = detail::kSogouGdiBaseDpi) {
  if (text.empty()) return true;
  if ((!target && !measured_width) || !format || text.size() > 4096 || !std::isfinite(font_size) ||
      font_size < 8 || font_size > 96) return false;
  struct Resources {
    HDC dc = CreateCompatibleDC(nullptr);
    HFONT font = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ old_font = nullptr;
    HGDIOBJ old_bitmap = nullptr;
    ~Resources() {
      if (old_bitmap) SelectObject(dc, old_bitmap);
      if (old_font) SelectObject(dc, old_font);
      if (bitmap) DeleteObject(bitmap);
      if (font) DeleteObject(font);
      if (dc) DeleteDC(dc);
    }
  } resources;
  if (!resources.dc) return false;
  float render_dpi_x = measure_dpi;
  float render_dpi_y = measure_dpi;
  if (target) target->GetDpi(&render_dpi_x, &render_dpi_y);
  const auto raster = detail::ResolveSogouGdiRasterScale(render_dpi_y);
  std::wstring family(format->GetFontFamilyNameLength() + 1, L'\0');
  if (FAILED(format->GetFontFamilyName(family.data(), static_cast<UINT32>(family.size()))))
    return false;
  resources.font = CreateFontW(-raster.ToPhysicalPixels(font_size), 0, 0, 0,
      FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
      CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, family.c_str());
  if (!resources.font) return false;
  resources.old_font = SelectObject(resources.dc, resources.font);
  if (!resources.old_font || resources.old_font == HGDI_ERROR) {
    resources.old_font = nullptr;
    return false;
  }
  TEXTMETRICW metrics{};
  SIZE extent{};
  if (!GetTextMetricsW(resources.dc, &metrics) ||
      !GetTextExtentPoint32W(resources.dc, text.data(), static_cast<int>(text.size()), &extent))
    return false;
  std::vector<int> advances;
  if (ink_advance) {
    // The distinct Latin preedit path advances to the raster's right edge,
    // excluding the glyph's unused right side bearing. Keep this separate
    // from the native candidate runs, which use ordinary GDI advances.
    MAT2 identity{};
    identity.eM11.value = 1;
    identity.eM22.value = 1;
    extent.cx = 0;
    for (const wchar_t character : text) {
      if (character < 0x20 || character > 0x7e) return false;
      GLYPHMETRICS glyph{};
      if (GetGlyphOutlineW(resources.dc, character, GGO_GRAY8_BITMAP, &glyph,
                           0, nullptr, &identity) == GDI_ERROR) return false;
      const LONG advance = glyph.gmBlackBoxX == 0
          ? static_cast<LONG>(glyph.gmCellIncX)
          : glyph.gmptGlyphOrigin.x + static_cast<LONG>(glyph.gmBlackBoxX);
      advances.push_back(static_cast<int>(std::max(advance, 1L)));
      extent.cx += advances.back();
      if (extent.cx > 16384) return false;
    }
  }
  LONG prefix_slot = 0;
  if (!prefix.empty()) {
    SIZE first_label{};
    if (!GetTextExtentPoint32W(resources.dc, L"1.", 2, &first_label)) return false;
    // The independent H1 number column uses the first label's advance plus
    // its two-pixel separation, observed in both A and B native compositions.
    prefix_slot = first_label.cx + raster.ToPhysicalPixels(2.0F);
  }
  const LONG width = std::max(extent.cx + prefix_slot, 1L);
  const LONG height = metrics.tmHeight;
  if (raster.ToLogicalPixels(width) > 16384.0F || height <= 0 ||
      raster.ToLogicalPixels(height) > 512.0F)
    return false;
  if (measured_width) *measured_width = raster.ToLogicalPixels(width);
  if (measured_height)
    *measured_height = std::min(raster.ToLogicalPixels(height), font_size);
  if (!target) return true;
  BITMAPINFO info{};
  info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biWidth = width;
  info.bmiHeader.biHeight = -height;
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;
  void* bits = nullptr;
  resources.bitmap = CreateDIBSection(resources.dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
  if (!resources.bitmap || !bits) return false;
  resources.old_bitmap = SelectObject(resources.dc, resources.bitmap);
  if (!resources.old_bitmap || resources.old_bitmap == HGDI_ERROR) {
    resources.old_bitmap = nullptr;
    return false;
  }
  auto* pixels = static_cast<std::uint32_t*>(bits);
  const std::size_t count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
  std::fill_n(pixels, count, 0U);
  SetBkMode(resources.dc, TRANSPARENT);
  SetTextColor(resources.dc, RGB(255, 255, 255));
  if (!prefix.empty() && !ExtTextOutW(resources.dc, 0, 0, ETO_IGNORELANGUAGE,
      nullptr, prefix.data(), static_cast<UINT>(prefix.size()), nullptr)) return false;
  if (!ExtTextOutW(resources.dc, prefix_slot, 0, ETO_IGNORELANGUAGE, nullptr, text.data(),
                   static_cast<UINT>(text.size()),
                   advances.empty() ? nullptr : advances.data())) return false;
  GdiFlush();
  const auto channel = [](float v) {
    return static_cast<std::uint32_t>(std::lround(std::clamp(v, 0.0F, 1.0F) * 255.0F));
  };
  const auto red = channel(color.r), green = channel(color.g), blue = channel(color.b);
  const auto opacity = channel(color.a);
  for (std::size_t i = 0; i < count; ++i) {
    const auto pixel = pixels[i];
    const auto coverage = ((pixel & 255U) + ((pixel >> 8) & 255U) + ((pixel >> 16) & 255U)) / 3U;
    const auto alpha = (coverage * opacity + 127U) / 255U;
    pixels[i] = (alpha << 24) | (((red * alpha + 127U) / 255U) << 16) |
                (((green * alpha + 127U) / 255U) << 8) | ((blue * alpha + 127U) / 255U);
  }
  Microsoft::WRL::ComPtr<ID2D1Bitmap> bitmap;
  const auto properties = D2D1::BitmapProperties(
      D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
      render_dpi_x, raster.dpi);
  if (FAILED(target->CreateBitmap(D2D1::SizeU(static_cast<UINT32>(width), static_cast<UINT32>(height)),
      pixels, static_cast<UINT32>(width) * 4U, properties, &bitmap))) return false;
  // The SSF cell ends at font_size-1, not at the full GDI line-cell bottom.
  // Preserve ascenders above the nominal origin instead of clipping them.
  const auto horizontal_raster = detail::ResolveSogouGdiRasterScale(render_dpi_x);
  const float logical_raster_width = horizontal_raster.ToLogicalPixels(width);
  const float logical_raster_height = raster.ToLogicalPixels(height);
  const float draw_top = clip.top +
      (ink_advance ? font_size - logical_raster_height - 1.0F : 0.0F);
  auto ink_clip = clip;
  ink_clip.top = std::min(clip.top, draw_top);
  target->PushAxisAlignedClip(ink_clip, D2D1_ANTIALIAS_MODE_ALIASED);
  target->DrawBitmap(bitmap.Get(), D2D1::RectF(clip.left, draw_top,
      clip.left + logical_raster_width, draw_top + logical_raster_height),
      1.0F, D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
  target->PopAxisAlignedClip();
  return true;
}

bool RegisterCandidateWindowClass(bool preview) {
  static std::array<std::once_flag, 2> once;
  static std::array<bool, 2> result{};
  const std::size_t index = preview ? 1 : 0;
  std::call_once(once[index], [preview, index] {
    WNDCLASSEXW window_class{sizeof(window_class)};
    window_class.style = CS_HREDRAW | CS_VREDRAW;
    window_class.lpfnWndProc = CandidateWindow::WindowProcedure;
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    // Settings can also load the real TIP. Do not share its window class.
    window_class.lpszClassName = preview ? kCandidatePreviewClass : kCandidateWindowClass;
    result[index] = RegisterClassExW(&window_class) != 0;
  });
  return result[index];
}

}  // namespace

CandidateWindow::~CandidateWindow() {
  StopCaretPosition();
  if (window_ != nullptr) {
    DestroyWindow(window_);
  }
}

bool CandidateWindow::Create(HWND owner) {
  return CreateInternal(owner, false);
}

bool CandidateWindow::CreatePreview(HWND owner) {
  return CreateInternal(owner, true);
}

bool CandidateWindow::CreateInternal(HWND owner, bool preview) {
  if (preview && (owner == nullptr || !IsWindow(owner))) {
    return false;
  }
  if (window_ != nullptr) {
    return preview_mode_ == preview && (!preview || GetParent(window_) == owner);
  }
  if (!RegisterCandidateWindowClass(preview)) {
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
  static_cast<void>(EnsureImagingFactory());

  preview_mode_ = preview;
  const DWORD extended_style = WS_EX_NOACTIVATE | (preview ? 0 : WS_EX_TOOLWINDOW);
  const DWORD style = preview ? WS_CHILD | WS_CLIPSIBLINGS : WS_POPUP;
  window_ = CreateWindowExW(extended_style,
                            preview ? kCandidatePreviewClass : kCandidateWindowClass, L"", style, 0, 0,
                            static_cast<int>(kVerticalWindowWidth), 64, owner, nullptr,
                            GetModuleHandleW(nullptr), this);
  if (window_ == nullptr) {
    return false;
  }

  if (!preview) {
    caret_mailbox_ = std::make_shared<CandidateCaretMailbox>();
    caret_mailbox_->window = window_;
    const DWM_WINDOW_CORNER_PREFERENCE preference = DWMWCP_ROUND;
    DwmSetWindowAttribute(window_, DWMWA_WINDOW_CORNER_PREFERENCE, &preference,
                          sizeof(preference));
  }
  return true;
}

void CandidateWindow::Show(const core::CompositionSnapshot& snapshot,
                           const RECT& text_rectangle, const core::Settings& settings,
                           std::size_t page_offset) {
  DWORD process = 0;
  GetWindowThreadProcessId(GetForegroundWindow(), &process);
  if (preview_mode_ || !window_ || snapshot.empty() || process != GetCurrentProcessId()) {
    ShowInternal(snapshot, text_rectangle, nullptr, settings, page_offset);
    return;
  }
  QueuePresentation(snapshot, text_rectangle, settings, page_offset);
  SetTimer(window_, kCaretPositionTimer, 100, nullptr);
  RequestCaretPosition();
  // Content updates do not get to overwrite a verified position with the TSF
  // fallback. On first show, wait for the asynchronous position decision.
  if (verified_caret_) {
    PresentAtCaret(verified_caret_);
  }
}

void CandidateWindow::QueuePresentation(const core::CompositionSnapshot& snapshot,
                                        const RECT& fallback, const core::Settings& settings,
                                        std::size_t page_offset) {
  pending_presentation_ = PendingPresentation{snapshot, fallback, settings, page_offset};
  candidate_requested_visible_ = true;
  presentation_dirty_ = true;
}

void CandidateWindow::PresentAtCaret(const std::optional<RECT>& rectangle) {
  if (!candidate_requested_visible_ || !pending_presentation_) {
    return;
  }
  if (rectangle) {
    verified_caret_ = rectangle;
  }
  // A transient provider failure must not resurrect a stale TSF rectangle.
  const RECT anchor = verified_caret_.value_or(pending_presentation_->fallback);
  if (presentation_dirty_ || !IsWindowVisible(window_) || !EqualRect(&anchor, &text_rectangle_)) {
    const auto presentation = *pending_presentation_;
    presentation_dirty_ = false;
    ShowInternal(presentation.snapshot, anchor, nullptr, presentation.settings,
                 presentation.page_offset);
  }
}

void CandidateWindow::ShowPreview(const core::CompositionSnapshot& snapshot,
                                  const RECT& preview_bounds,
                                  const core::Settings& settings,
                                  std::size_t page_offset,
                                  const RECT* viewport_bounds) {
  if (!preview_mode_ || preview_bounds.right <= preview_bounds.left ||
      preview_bounds.bottom <= preview_bounds.top) {
    Hide();
    return;
  }
  const RECT empty_text_rectangle{};
  ShowInternal(snapshot, empty_text_rectangle, &preview_bounds, settings, page_offset);
  if (window_ == nullptr) {
    return;
  }
  // The XAML host and viewport are in owner-client physical pixels. Keep the
  // child in that space, including when the ScrollViewer clips part of it.
  RECT child_bounds{};
  GetWindowRect(window_, &child_bounds);
  MapWindowPoints(nullptr, GetParent(window_), reinterpret_cast<POINT*>(&child_bounds), 2);
  RECT visible{};
  const RECT& clip = viewport_bounds == nullptr ? preview_bounds : *viewport_bounds;
  if (!IntersectRect(&visible, &child_bounds, &clip)) {
    Hide();
    return;
  }
  HRGN region = CreateRectRgn(visible.left - child_bounds.left, visible.top - child_bounds.top,
                             visible.right - child_bounds.left, visible.bottom - child_bounds.top);
  if (region != nullptr && SetWindowRgn(window_, region, TRUE) == 0) {
    DeleteObject(region);
  }
}

void CandidateWindow::ShowInternal(const core::CompositionSnapshot& snapshot,
                                   const RECT& text_rectangle, const RECT* preview_bounds,
                                   const core::Settings& persisted_settings,
                                   std::size_t page_offset) {
  if (window_ == nullptr || snapshot.empty()) {
    Hide();
    return;
  }
  const core::Settings settings =
      core::ResolveEffectiveCandidateAppearanceSettings(persisted_settings);
  const bool was_visible = IsWindowVisible(window_) != FALSE;
  candidate_requested_visible_ = true;
  const bool was_hiding = hide_after_fade_;
  RECT previous_rectangle{};
  GetWindowRect(window_, &previous_rectangle);

  const bool theme_changed =
      !theme_initialized_ || settings_.active_theme_id != settings.active_theme_id;
  if (theme_changed) {
    RefreshTheme(settings.active_theme_id);
  }
  // Every candidate surface uses per-pixel alpha. The native surface needs it
  // for deterministic anti-aliased corners; relying on the DWM border made the
  // same build render square in some Windows 11 sessions.
  constexpr bool kUseLayeredRendering = true;
  const bool rendering_mode_changed =
      layered_rendering_enabled_ != kUseLayeredRendering;
  if (rendering_mode_changed) {
    DiscardDeviceResources();
    layered_rendering_enabled_ = kUseLayeredRendering;
    ApplyWindowRenderingMode();
  }
  const bool resolved_dark_theme = UseDarkTheme(settings.theme_mode);
  const bool appearance_changed =
      theme_changed || !dark_theme_initialized_ || dark_theme_ != resolved_dark_theme ||
      settings_.theme_mode != settings.theme_mode ||
      settings_.custom_candidate_colors != settings.custom_candidate_colors ||
      settings_.preedit_color != settings.preedit_color ||
      settings_.highlighted_candidate_color != settings.highlighted_candidate_color ||
      settings_.candidate_text_color != settings.candidate_text_color ||
      settings_.candidate_background_color != settings.candidate_background_color ||
      settings_.candidate_layout != settings.candidate_layout ||
      settings_.custom_candidate_fonts != settings.custom_candidate_fonts ||
      settings_.candidate_chinese_font_family != settings.candidate_chinese_font_family ||
      settings_.candidate_english_font_family != settings.candidate_english_font_family ||
      settings_.custom_candidate_font_size != settings.custom_candidate_font_size ||
      settings_.candidate_font_size != settings.candidate_font_size ||
      settings_.candidate_scale_with_text != settings.candidate_scale_with_text;
  snapshot_ = snapshot;
  settings_ = settings;
  text_rectangle_ = text_rectangle;
  dark_theme_ = resolved_dark_theme;
  dark_theme_initialized_ = true;
  const auto& theme_typography = ActiveThemeAppearance().typography;
  const float effective_font_size = static_cast<float>(
      settings_.custom_candidate_font_size
          ? std::clamp(settings_.candidate_font_size, core::kMinimumCandidateFontSize,
                       core::kMaximumCandidateFontSize)
          : std::clamp<std::uint32_t>(
                theme_typography.font_size,
                static_cast<std::uint32_t>(core::kMinimumCandidateFontSize),
                static_cast<std::uint32_t>(core::kMaximumCandidateFontSize)));
  // SSF insets are already authored for the skin's declared font size. Only a
  // user font-size override may rescale them; DPI is applied separately below.
  layout_scale_ = settings_.candidate_scale_with_text &&
                          (!UsesSogouRendering() || settings_.custom_candidate_font_size)
                      ? std::clamp(effective_font_size / 17.0F, 0.82F, 1.42F)
                      : 1.0F;
  const bool horizontal =
      settings_.candidate_layout == core::CandidateLayout::kHorizontal;
  const auto scale_insets = [this](const core::ThemeInsets& insets) {
    const float scale = ThemeUnitScale();
    return ScaledInsets{static_cast<float>(insets.left) * scale,
                        static_cast<float>(insets.top) * scale,
                        static_cast<float>(insets.right) * scale,
                        static_cast<float>(insets.bottom) * scale};
  };
  const auto& surface = ActiveThemeSurface();
  const bool native_default = UsesNativeDefaultTheme();
  const bool compact = native_default && horizontal;
  const bool ssf_native_text = horizontal && UsesSogouRendering() &&
      theme_typography.sogou_use_gdip == 1U &&
      !settings_.custom_candidate_fonts && !settings_.custom_candidate_font_size;
  const bool ssf_vertical_text = !horizontal && UsesSogouRendering() &&
      theme_typography.sogou_use_gdip == 1U &&
      !settings_.custom_candidate_fonts && !settings_.custom_candidate_font_size;
  const bool animate = NativeAnimationsEnabled();
  if (!animate) {
    KillTimer(window_, kNativeFadeTimer);
    fade_active_ = false;
    surface_opacity_ = 1.0F;
  }
  hide_after_fade_ = false;
  preedit_insets_ =
      surface.preedit_insets.has_value()
          ? scale_insets(*surface.preedit_insets)
          : ScaledInsets{kHorizontalPadding * layout_scale_,
                         10.0F * layout_scale_,
                         kHorizontalPadding * layout_scale_,
                         (horizontal ? 6.0F : 10.0F) * layout_scale_};
  candidate_insets_ =
      surface.candidate_insets.has_value()
          ? scale_insets(*surface.candidate_insets)
          : ScaledInsets{8.0F * layout_scale_,
                         (horizontal ? 4.0F : kHorizontalPadding) * layout_scale_,
                         8.0F * layout_scale_,
                         (horizontal ? 4.0F : kHorizontalPadding) * layout_scale_};
  preedit_height_ =
      std::max((horizontal ? kHorizontalPreeditHeight : kPreeditHeight) * layout_scale_,
               preedit_insets_.top + effective_font_size + preedit_insets_.bottom);
  candidate_row_height_ =
      std::max((horizontal ? kHorizontalCandidateHeight : kCandidateHeight) * layout_scale_,
               effective_font_size + 8.0F);
  if (compact) {
    preedit_insets_ = {10.0F * layout_scale_, 4.0F * layout_scale_,
                      10.0F * layout_scale_, 3.0F * layout_scale_};
    candidate_insets_ = {4.0F * layout_scale_, 2.0F * layout_scale_,
                        4.0F * layout_scale_, 3.0F * layout_scale_};
  }
  if (appearance_changed) {
    DiscardDeviceResources();
  }
  const auto active_slice = core::MakeCandidatePageSlice(
      snapshot_.candidates.size(), settings_.candidate_count, page_offset);
  page_offset_ = active_slice.offset;
  can_expand_ =
      horizontal && settings_.candidate_page_mode == core::CandidatePageMode::kMultiLine &&
      snapshot_.candidates.size() > active_slice.count;
  if (!can_expand_) {
    expanded_ = false;
  }
  const auto page_window = core::MakeCandidatePageWindow(
      snapshot_.candidates.size(), settings_.candidate_count, page_offset_, expanded_);
  const auto slice = page_window.visible;
  const UINT dpi = std::max(GetDpiForWindow(window_), static_cast<UINT>(USER_DEFAULT_SCREEN_DPI));
  dpi_scale_ = static_cast<float>(dpi) / static_cast<float>(USER_DEFAULT_SCREEN_DPI);
  if (render_target_ != nullptr) {
    render_target_->SetDpi(static_cast<float>(dpi), static_cast<float>(dpi));
  }

  RECT work_area{};
  if (preview_bounds != nullptr) {
    work_area = *preview_bounds;
  } else {
    const POINT monitor_point{text_rectangle.left, text_rectangle.bottom};
    const HMONITOR monitor = MonitorFromPoint(monitor_point, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitor_info{sizeof(monitor_info)};
    if (!GetMonitorInfoW(monitor, &monitor_info)) {
      monitor_info.rcWork =
          RECT{0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
    }
    work_area = monitor_info.rcWork;
  }
  const int work_left = static_cast<int>(work_area.left);
  const int work_top = static_cast<int>(work_area.top);
  const int work_right = static_cast<int>(work_area.right);
  const int work_bottom = static_cast<int>(work_area.bottom);
  const int work_width = work_right - work_left;
  const int work_height = work_bottom - work_top;
  const float available_width = static_cast<float>(work_width) / dpi_scale_;
  const float available_height = static_cast<float>(work_height) / dpi_scale_;
  shadow_margin_ = native_default
      ? std::min(12.0F * layout_scale_, std::max(0.0F, (std::min(available_width, available_height) - 1.0F) * 0.5F))
      : 0.0F;
  const float maximum_window_width = std::max(1.0F, available_width - 2.0F * shadow_margin_);
  const float maximum_window_height = std::max(1.0F, available_height - 2.0F * shadow_margin_);
  const auto fit_horizontal_insets = [](ScaledInsets* insets,
                                        float maximum_total) {
    const float total = insets->left + insets->right;
    if (total > maximum_total && total > 0.0F) {
      const float scale = std::max(maximum_total, 0.0F) / total;
      insets->left *= scale;
      insets->right *= scale;
    }
  };
  const auto fit_vertical_insets = [](ScaledInsets* insets,
                                      float maximum_total) {
    const float total = insets->top + insets->bottom;
    if (total > maximum_total && total > 0.0F) {
      const float scale = std::max(maximum_total, 0.0F) / total;
      insets->top *= scale;
      insets->bottom *= scale;
    }
  };
  fit_vertical_insets(
      &preedit_insets_,
      maximum_window_height - effective_font_size - candidate_row_height_);
  preedit_height_ =
      std::max((horizontal ? kHorizontalPreeditHeight : kPreeditHeight) *
                   layout_scale_,
               preedit_insets_.top + effective_font_size +
                   preedit_insets_.bottom);
  fit_vertical_insets(
      &candidate_insets_,
      maximum_window_height - preedit_height_ - candidate_row_height_);
  fit_horizontal_insets(
      &preedit_insets_,
      maximum_window_width - 64.0F * layout_scale_);
  const bool use_native_actions = !UsesSogouRendering();
  const bool show_menu_action =
      horizontal && (use_native_actions || surface.menu_button.has_value());
  const bool show_expand_action =
      horizontal && can_expand_ &&
      (use_native_actions || surface.expand_button.has_value() ||
       surface.collapse_button.has_value());
  const float menu_width = compact ? 32.0F : kHorizontalMenuButtonWidth;
  const float expand_width = compact ? 28.0F : kHorizontalExpandButtonWidth;
  const float minimum_candidate_width = ssf_native_text ? 1.0F :
      (compact ? 48.0F : kMinimumHorizontalCandidateWidth);
  const std::wstring_view label_gap = horizontal && UsesSogouRendering()
      ? L"." : (compact ? L" " : L"  ");
  // Custom H1 still reserves its reference action strip when no button
  // images are supplied. Omitting that area lets the final word enter the
  // fixed right-hand artwork; this does not draw a standalone default skin.
  const float reserved_action_width =
      ((show_menu_action ? menu_width : 0.0F) +
       (show_expand_action ? expand_width : 0.0F)) *
      layout_scale_ + (ssf_native_text && !show_menu_action ? 63.0F : 0.0F);
  const std::size_t visible_columns =
      horizontal ? std::max<std::size_t>(
                       1, std::min(slice.count, settings_.candidate_count))
                 : 1;
  fit_horizontal_insets(
      &candidate_insets_,
      maximum_window_width - reserved_action_width -
          minimum_candidate_width * layout_scale_ *
              static_cast<float>(visible_columns));
  const bool has_device_resources = EnsureDeviceResources();
  if (ssf_vertical_text) {
    // The retained 20px/24px custom V1 cells have 24px/28px row advances.
    // Do not impose Ziliu's native 38px minimum on authored SSF text.
    preedit_height_ = preedit_insets_.top + effective_font_size + preedit_insets_.bottom;
    float preedit_width = 0.0F;
    float preedit_cell_height = effective_font_size;
    static_cast<void>(DrawSsfGdiText(nullptr, preedit_format_.Get(), snapshot_.preedit,
        effective_font_size, {}, {}, {}, &preedit_width, false, &preedit_cell_height,
        dpi_scale_ * detail::kSogouGdiBaseDpi));
    preedit_height_ = preedit_insets_.top + preedit_cell_height + preedit_insets_.bottom;
    candidate_row_height_ = effective_font_size + 4.0F;
  }
  float native_preedit_width = 0.0F;
  native_preedit_layout_.Reset();
  if (native_default && has_device_resources) {
    auto preedit = detail::MeasureNativeTextLine(dwrite_factory_.Get(), preedit_format_.Get(),
                                                snapshot_.preedit, maximum_window_width, dpi_scale_);
    if (preedit.layout != nullptr) {
      native_preedit_layout_ = std::move(preedit.layout);
      native_preedit_origin_offset_ = preedit.origin_offset;
      native_preedit_width = preedit.width;
      preedit_height_ = preedit_insets_.top + preedit.height + preedit_insets_.bottom;
    }
    float candidate_ink_height = effective_font_size;
    for (std::size_t index = slice.offset; index < slice.offset + slice.count; ++index) {
      const std::wstring label = std::to_wstring(index - slice.offset + 1) +
                                 std::wstring(label_gap) + snapshot_.candidates[index].text;
      const auto candidate = detail::MeasureNativeTextLine(
          dwrite_factory_.Get(), candidate_format_.Get(), label, maximum_window_width, dpi_scale_);
      candidate_ink_height = std::max(candidate_ink_height, candidate.height);
    }
    candidate_row_height_ = compact
        ? detail::NativeCandidateRowHeight(candidate_ink_height, layout_scale_)
        : std::max(candidate_row_height_, candidate_ink_height + 4.0F * layout_scale_);
  }
  D2D1_SIZE_F natural_background_size{};
  if (UsesSogouRendering() && surface.background.has_value() &&
      surface_bitmaps_.background != nullptr) {
    const D2D1_SIZE_F bitmap_size = surface_bitmaps_.background->GetSize();
    natural_background_size =
        D2D1::SizeF(bitmap_size.width * layout_scale_,
                    bitmap_size.height * layout_scale_);
  }
  float measured_preedit_width =
      native_default && native_preedit_layout_ != nullptr
          ? native_preedit_width
          : has_device_resources
          ? MeasureTextWidth(dwrite_factory_.Get(), preedit_format_.Get(), snapshot_.preedit,
                             maximum_window_width, preedit_height_)
          : 0.0F;
  if (ssf_vertical_text && has_device_resources) {
    static_cast<void>(DrawSsfGdiText(nullptr, preedit_format_.Get(), snapshot_.preedit,
        effective_font_size, {}, {}, {}, &measured_preedit_width,
        theme_typography.english_font_family != theme_typography.chinese_font_family, nullptr,
        dpi_scale_ * detail::kSogouGdiBaseDpi));
    measured_preedit_width += 1.0F;  // Reserve the drawn caret as well as glyph advances.
  }
  const float desired_preedit_window_width =
      measured_preedit_width + preedit_insets_.left + preedit_insets_.right;

  candidate_indices_.clear();
  candidate_widths_.clear();
  candidate_lefts_.clear();
  candidate_tops_.clear();
  expand_button_bounds_ = {};
  menu_button_bounds_ = {};
  std::size_t horizontal_rows = horizontal ? page_window.row_count : 0;
  if (horizontal) {
    candidate_indices_.reserve(slice.count);
    for (std::size_t visible_index = 0; visible_index < slice.count; ++visible_index) {
      candidate_indices_.push_back(slice.offset + visible_index);
    }
    if (has_device_resources) {
      for (std::size_t visible_index = 0; visible_index < slice.count; ++visible_index) {
        const std::size_t candidate_index = slice.offset + visible_index;
        const bool active = candidate_index >= page_window.active.offset &&
                            candidate_index < page_window.active.offset + page_window.active.count;
        const std::wstring label =
            active
                ? std::to_wstring(candidate_index - page_window.active.offset + 1) + std::wstring(label_gap) +
                      snapshot_.candidates[candidate_index].text
                : snapshot_.candidates[candidate_index].text;
        const float measured_width =
            MeasureTextWidth(dwrite_factory_.Get(), candidate_format_.Get(), label,
                             maximum_window_width, candidate_row_height_);
        float desired_width = measured_width + (compact ? 16.0F : 24.0F) * layout_scale_;
        if (ssf_native_text) {
          // Native H1 runs include a trailing space and a ten-pixel cell gap.
          // Use the same font and independent number slot as the paint path.
          const std::wstring body = snapshot_.candidates[candidate_index].text + L" ";
          float native_width = 0.0F;
          if (DrawSsfGdiText(nullptr, candidate_format_.Get(), body, effective_font_size,
                            {}, {}, active ? L"1." : L"", &native_width, false, nullptr,
                            dpi_scale_ * detail::kSogouGdiBaseDpi)) {
            desired_width = native_width + 10.0F;
          }
        }
        const float width =
            std::clamp(desired_width,
                       minimum_candidate_width * layout_scale_, maximum_window_width);
        candidate_widths_.push_back(width);
      }
    }
    if (candidate_widths_.empty() && slice.count != 0) {
      candidate_widths_.assign(slice.count,
                               minimum_candidate_width * layout_scale_);
    }
    const float outer_width = candidate_insets_.left + candidate_insets_.right;
    const float action_width = reserved_action_width;
    const std::size_t columns = settings_.candidate_count;
    float widest_row = 0.0F;
    for (std::size_t row = 0; row < horizontal_rows; ++row) {
      const std::size_t begin = row * columns;
      const std::size_t end = std::min(begin + columns, candidate_widths_.size());
      if (begin >= end) {
        continue;
      }
      std::vector<float> row_widths(candidate_widths_.begin() + static_cast<std::ptrdiff_t>(begin),
                                    candidate_widths_.begin() + static_cast<std::ptrdiff_t>(end));
      const bool active_row =
          begin < candidate_indices_.size() &&
          candidate_indices_[begin] <= page_window.active.offset &&
          page_window.active.offset <= candidate_indices_[end - 1];
      FitCandidateWidths(&row_widths, maximum_window_width - outer_width - action_width,
                         minimum_candidate_width * layout_scale_, active_row);
      std::copy(row_widths.begin(), row_widths.end(),
                candidate_widths_.begin() + static_cast<std::ptrdiff_t>(begin));
      widest_row = std::max(
          widest_row, std::accumulate(row_widths.begin(), row_widths.end(), 0.0F));
    }
    window_width_ = std::clamp(
        std::max(outer_width + widest_row + action_width, desired_preedit_window_width),
        std::min((compact ? 180.0F : kMinimumHorizontalWindowWidth) * layout_scale_, maximum_window_width),
        maximum_window_width);
    for (std::size_t row = 0; row < horizontal_rows; ++row) {
      float left = candidate_insets_.left;
      const std::size_t begin = row * columns;
      const std::size_t end = std::min(begin + columns, candidate_widths_.size());
      for (std::size_t index = begin; index < end; ++index) {
        candidate_lefts_.push_back(left);
        candidate_tops_.push_back(preedit_height_ + candidate_insets_.top +
                                  static_cast<float>(row) * candidate_row_height_);
        left += candidate_widths_[index];
      }
    }
  } else {
    candidate_indices_.reserve(slice.count);
    for (std::size_t visible_index = 0; visible_index < slice.count; ++visible_index) {
      candidate_indices_.push_back(slice.offset + visible_index);
    }
    const float base_vertical_width =
        UsesSogouRendering() && natural_background_size.width > 0.0F
            ? natural_background_size.width
            : kVerticalWindowWidth * layout_scale_;
    float desired_vertical_width =
        std::max(base_vertical_width, desired_preedit_window_width);
    if (has_device_resources) {
      for (std::size_t visible_index = 0; visible_index < slice.count; ++visible_index) {
        const std::size_t candidate_index = slice.offset + visible_index;
        const std::wstring label = std::to_wstring(visible_index + 1) +
                                   (ssf_vertical_text ? L"." : L"  ") +
                                   snapshot_.candidates[candidate_index].text;
        float candidate_width =
            MeasureTextWidth(dwrite_factory_.Get(), candidate_format_.Get(), label,
                             maximum_window_width, candidate_row_height_);
        if (ssf_vertical_text) {
          static_cast<void>(DrawSsfGdiText(nullptr, candidate_format_.Get(), label,
              effective_font_size, {}, {}, {}, &candidate_width, false, nullptr,
              dpi_scale_ * detail::kSogouGdiBaseDpi));
        }
        desired_vertical_width =
            std::max(desired_vertical_width,
                     candidate_width + candidate_insets_.left +
                         candidate_insets_.right +
                         (UsesSogouRendering() ? 0.0F
                                               : 120.0F * layout_scale_));
      }
    }
    window_width_ = std::min(desired_vertical_width, maximum_window_width);
  }

  float height_dip =
      slice.count == 0
          ? (compact ? preedit_height_ : std::max(preedit_height_,
                     kHorizontalPreeditOnlyHeight * layout_scale_))
          : preedit_height_ + candidate_insets_.top +
                candidate_row_height_ *
                    static_cast<float>(horizontal ? horizontal_rows : slice.count) +
                candidate_insets_.bottom;
  if (ssf_vertical_text && slice.count != 0) {
    // Two retained custom V1 references reserve the same 52px pager/menu footer.
    // The text rows remain font-sized; this footer is not candidate leading.
    height_dip += 52.0F;
    const float rows_bottom = preedit_height_ + candidate_insets_.top +
        candidate_row_height_ * static_cast<float>(slice.count);
    menu_button_bounds_ = D2D1::RectF(candidate_insets_.left, rows_bottom + 28.0F,
        candidate_insets_.left + 28.0F, rows_bottom + 56.0F);
  }
  if (UsesSogouRendering() && surface.background.has_value()) {
    if (natural_background_size.width > 0.0F) {
      window_width_ =
          std::min(std::max(window_width_, natural_background_size.width),
                   maximum_window_width);
    }
    if (natural_background_size.height > 0.0F) {
      if (surface.background->vertical_layout == core::ThemeImageLayout::kFixed) {
        height_dip = std::min(natural_background_size.height, maximum_window_height);
      } else if (horizontal) {
        // H1 background artwork supplies a minimum height even when its center
        // is stretchable. Short candidate rows must not compress the artwork.
        height_dip = std::max(height_dip, natural_background_size.height);
      }
    }
  }
  if (horizontal && reserved_action_width > 0.0F) {
    const float button_top = preedit_height_ + candidate_insets_.top;
    const float button_bottom = button_top + candidate_row_height_;
    if (show_menu_action) {
      menu_button_bounds_ =
          D2D1::RectF(window_width_ -
                          menu_width * layout_scale_,
                      button_top, window_width_, button_bottom);
    }
    if (show_expand_action) {
      const float right =
          show_menu_action ? menu_button_bounds_.left : window_width_;
      expand_button_bounds_ =
          D2D1::RectF(right -
                          expand_width * layout_scale_,
                      button_top, right, button_bottom);
    }
  }
  if (ssf_native_text && !show_menu_action && !candidate_lefts_.empty()) {
    // Use the already reserved action strip without resizing or moving any
    // accepted text/artwork. Reuse the existing quick-menu hit-test callback.
    const float left = candidate_lefts_.back() + candidate_widths_.back();
    const float top = candidate_tops_.back();
    menu_button_bounds_ = D2D1::RectF(left, top - 2.0F,
        std::min(left + 28.0F, window_width_), top + effective_font_size + 2.0F);
  }
  window_height_ = std::min(height_dip, maximum_window_height);
  int width = ToPixels(window_width_ + 2.0F * shadow_margin_, dpi_scale_);
  int height = ToPixels(window_height_ + 2.0F * shadow_margin_, dpi_scale_);
  width = std::min(width, work_width);
  height = std::min(height, work_height);

  int x = 0;
  int y = 0;
  HWND insert_after = HWND_TOPMOST;
  UINT position_flags = SWP_NOACTIVATE | SWP_SHOWWINDOW;
  if (preview_bounds != nullptr) {
    x = work_left + std::max((work_width - width) / 2, 0);
    y = work_top + std::max((work_height - height) / 2, 0);
    insert_after = HWND_TOP;
  } else {
    x = text_rectangle.left - ToPixels(shadow_margin_, dpi_scale_);
    y = text_rectangle.bottom + 2 - ToPixels(shadow_margin_, dpi_scale_);
    if (x + width > work_right) {
      x = work_right - width;
    }
    x = std::max(x, work_left);
    if (y + height > work_bottom) {
      y = text_rectangle.top - height - 2 + ToPixels(shadow_margin_, dpi_scale_);
    }
    y = std::clamp(y, work_top, work_bottom - height);
  }

  if (animate) {
    if (!was_visible) {
      StartNativeFade(0.0F, 1.0F, 110, false);
    } else if (was_hiding) {
      StartNativeFade(surface_opacity_, 1.0F, 80, false);
    }
  }
  const RECT target_rectangle{x, y, x + width, y + height};
  const bool can_animate_width = animate && was_visible && !was_hiding &&
      height == previous_rectangle.bottom - previous_rectangle.top &&
      previous_rectangle.left >= work_left && previous_rectangle.right <= work_right;
  // Repeated snapshots at the same target must not restart the timer. New input
  // retargets from the last presented window rectangle, including mid-animation.
  if (!can_animate_width || !width_active_ || !EqualRect(&target_rectangle, &width_to_rectangle_)) {
    KillTimer(window_, kNativeWidthTimer);
    width_active_ = false;
    width_to_rectangle_ = target_rectangle;
    width_presented_rectangle_ = target_rectangle;
    if (can_animate_width && width != previous_rectangle.right - previous_rectangle.left) {
      width_from_rectangle_ = previous_rectangle;
      width_started_ms_ = NativeAnimationMilliseconds();
      width_active_ = SetTimer(window_, kNativeWidthTimer, kNativeWidthInterval, nullptr) != 0;
      if (width_active_) {
        width_presented_rectangle_ = detail::NativeWidthFrame(
            width_from_rectangle_, target_rectangle, 0.0F, kNativeWidthDuration);
      }
    }
  }
  const auto& presented = width_presented_rectangle_;
  SetWindowPos(window_, insert_after, presented.left, presented.top,
               presented.right - presented.left, height, position_flags);
  if (width_active_ && (layered_pixel_size_.cx < width || layered_pixel_size_.cy != height)) {
    DiscardDeviceResources();
  }
  if (preview_bounds != nullptr && !layered_rendering_enabled_) {
    const int corner_diameter = ToPixels(kCornerRadius * 2.0F, dpi_scale_);
    HRGN region =
        CreateRoundRectRgn(0, 0, width + 1, height + 1, corner_diameter, corner_diameter);
    if (region != nullptr && SetWindowRgn(window_, region, FALSE) == 0) {
      DeleteObject(region);
    }
  }
  layered_present_retry_attempted_ = false;
  InvalidateRect(window_, nullptr, FALSE);
}

void CandidateWindow::StopCaretPosition() {
  if (caret_mailbox_) {
    std::lock_guard lock(caret_mailbox_->mutex);
    caret_mailbox_->alive = false;
    caret_mailbox_->window = nullptr;
    ++caret_mailbox_->revision;
  }
}

void CandidateWindow::RequestCaretPosition() {
  const HWND foreground = GetForegroundWindow();
  DWORD foreground_process = 0;
  const DWORD thread = GetWindowThreadProcessId(foreground, &foreground_process);
  if (!caret_mailbox_ || !candidate_requested_visible_ ||
      foreground_process != GetCurrentProcessId()) {
    return;
  }
  GUITHREADINFO info{sizeof(info)};
  if (!GetGUIThreadInfo(thread, &info)) {
    PresentAtCaret(std::nullopt);
    return;
  }
  if (caret_foreground_ != foreground || caret_focus_ != info.hwndFocus) {
    caret_foreground_ = foreground;
    caret_focus_ = info.hwndFocus;
    verified_caret_.reset();
    presentation_dirty_ = true;
    {
      std::lock_guard lock(caret_mailbox_->mutex);
      ++caret_mailbox_->revision;
    }
    // Never animate an old input surface over a newly focused control.
    KillTimer(window_, kNativeWidthTimer);
    KillTimer(window_, kNativeFadeTimer);
    width_active_ = fade_active_ = hide_after_fade_ = false;
    ShowWindow(window_, SW_HIDE);
  }
  std::unique_lock lock(caret_mailbox_->mutex);
  if (caret_mailbox_->pending) {
    return;
  }
  ++caret_mailbox_->revision;
  if (!caret_mailbox_->alive || caret_mailbox_->pending) {
    return;
  }
  HMODULE module = nullptr;
  if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
          reinterpret_cast<const wchar_t*>(&QueryCaretOnWorker), &module)) {
    lock.unlock();
    PresentAtCaret(std::nullopt);
    return;
  }
  auto query = std::make_unique<CaretQuery>(CaretQuery{
      caret_mailbox_, caret_mailbox_->revision, module, foreground, info.hwndFocus});
  caret_mailbox_->pending = true;
  if (TrySubmitThreadpoolCallback(QueryCaretOnWorker, query.get(), nullptr)) {
    static_cast<void>(query.release());
  } else {
    caret_mailbox_->pending = false;
    FreeLibrary(module);
    lock.unlock();
    PresentAtCaret(std::nullopt);
  }
}

void CandidateWindow::ApplyCaretPosition() {
  if (!caret_mailbox_ || !candidate_requested_visible_) {
    return;
  }
  DWORD foreground_process = 0;
  GetWindowThreadProcessId(GetForegroundWindow(), &foreground_process);
  if (foreground_process != GetCurrentProcessId()) {
    return;
  }
  GUITHREADINFO info{sizeof(info)};
  const HWND foreground = GetForegroundWindow();
  if (foreground != caret_foreground_ ||
      !GetGUIThreadInfo(GetWindowThreadProcessId(foreground, nullptr), &info) ||
      info.hwndFocus != caret_focus_) {
    RequestCaretPosition();
    return;
  }
  std::optional<RECT> rectangle;
  bool stale = false;
  {
    std::lock_guard lock(caret_mailbox_->mutex);
    stale = caret_mailbox_->completed_revision != caret_mailbox_->revision;
    if (!stale) {
      rectangle = caret_mailbox_->rectangle;
    }
  }
  if (stale) {
    RequestCaretPosition();
  } else {
    PresentAtCaret(rectangle);
  }
}

void CandidateWindow::SetExpanded(bool expanded) {
  if (expanded_ == expanded) {
    return;
  }
  expanded_ = expanded;
  if (window_ != nullptr && !snapshot_.empty() && IsWindowVisible(window_)) {
    Show(snapshot_, text_rectangle_, settings_, page_offset_);
  }
}

void CandidateWindow::SetQuickMenuAction(std::function<void(POINT)> action) {
  quick_menu_action_ = std::move(action);
}

void CandidateWindow::Hide() {
  candidate_requested_visible_ = false;
  pending_presentation_.reset();
  verified_caret_.reset();
  presentation_dirty_ = false;
  caret_foreground_ = caret_focus_ = nullptr;
  if (window_) {
    KillTimer(window_, kCaretPositionTimer);
  }
  if (caret_mailbox_) {
    std::lock_guard lock(caret_mailbox_->mutex);
    ++caret_mailbox_->revision;
  }
  if (window_ != nullptr) {
    KillTimer(window_, kNativeWidthTimer);
    width_active_ = false;
    if (NativeAnimationsEnabled() && IsWindowVisible(window_)) {
      if (!hide_after_fade_) {
        StartNativeFade(surface_opacity_, 0.0F, 80, true);
      }
    } else {
      KillTimer(window_, kNativeFadeTimer);
      fade_active_ = false;
      hide_after_fade_ = false;
      surface_opacity_ = 1.0F;
      ShowWindow(window_, SW_HIDE);
    }
  }
  expanded_ = false;
  expand_button_hovered_ = false;
  menu_button_hovered_ = false;
  expand_button_pressed_ = false;
  menu_button_pressed_ = false;
  tracking_mouse_leave_ = false;
}

bool CandidateWindow::NativeAnimationsEnabled() const {
  BOOL enabled = FALSE;
  return !preview_mode_ && UsesNativeDefaultTheme() &&
         SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0) && enabled;
}

void CandidateWindow::StartNativeFade(float from, float to, UINT duration, bool hide_after) {
  fade_from_ = surface_opacity_ = from;
  fade_to_ = to;
  fade_started_ = GetTickCount64();
  fade_duration_ = duration;
  hide_after_fade_ = hide_after;
  fade_active_ = SetTimer(window_, kNativeFadeTimer, 16, nullptr) != 0;
  if (!fade_active_) {
    surface_opacity_ = to;
    hide_after_fade_ = false;
    if (hide_after) {
      ShowWindow(window_, SW_HIDE);
    }
  }
  static_cast<void>(PresentLayeredSurface());
}

void CandidateWindow::AdvanceNativeFade() {
  if (!fade_active_) {
    return;
  }
  const ULONGLONG elapsed = GetTickCount64() - fade_started_;
  surface_opacity_ = detail::NativeFadeOpacity(fade_from_, fade_to_,
                                               static_cast<float>(elapsed),
                                               static_cast<float>(fade_duration_));
  if (elapsed >= fade_duration_ || !NativeAnimationsEnabled()) {
    KillTimer(window_, kNativeFadeTimer);
    fade_active_ = false;
    surface_opacity_ = fade_to_;
    if (hide_after_fade_) {
      hide_after_fade_ = false;
      ShowWindow(window_, SW_HIDE);
      surface_opacity_ = 1.0F;
      return;
    }
  }
  static_cast<void>(PresentLayeredSurface());
}

float CandidateWindow::PresentedContentWidth() const {
  return width_active_
      ? std::max(1.0F, static_cast<float>(width_presented_rectangle_.right -
          width_presented_rectangle_.left) / dpi_scale_ - 2.0F * shadow_margin_)
      : window_width_;
}

void CandidateWindow::AdvanceNativeWidth() {
  if (!width_active_) {
    return;
  }
  const float elapsed = static_cast<float>(NativeAnimationMilliseconds() - width_started_ms_);
  if (elapsed >= kNativeWidthDuration || !NativeAnimationsEnabled()) {
    KillTimer(window_, kNativeWidthTimer);
    width_active_ = false;
    width_presented_rectangle_ = width_to_rectangle_;
  } else {
    width_presented_rectangle_ = detail::NativeWidthFrame(
        width_from_rectangle_, width_to_rectangle_, elapsed, kNativeWidthDuration);
  }
  const auto& frame = width_presented_rectangle_;
  SetWindowPos(window_, nullptr, frame.left, frame.top, frame.right - frame.left,
               frame.bottom - frame.top, SWP_NOACTIVATE | SWP_NOZORDER);
  InvalidateRect(window_, nullptr, FALSE);
  UpdateWindow(window_);
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
    case WM_ACTIVATEAPP:
      // A host can keep its TSF context focused while another app (e.g. Start)
      // becomes foreground. Its no-activate popup must not remain on that app.
      if (!wparam && !preview_mode_) {
        const bool requested = candidate_requested_visible_;
        auto pending = std::move(pending_presentation_);
        Hide();
        pending_presentation_ = std::move(pending);
        presentation_dirty_ = requested;
        candidate_requested_visible_ = requested;
      } else if (wparam && !preview_mode_) {
        if (candidate_requested_visible_) {
          SetTimer(window_, kCaretPositionTimer, 100, nullptr);
        }
        RequestCaretPosition();
      }
      return DefWindowProcW(window_, message, wparam, lparam);
    case kCaretPositionReady:
      if (wparam == reinterpret_cast<WPARAM>(caret_mailbox_.get())) {
        ApplyCaretPosition();
      }
      return 0;
    case WM_TIMER:
      if (wparam == kCaretPositionTimer) {
        RequestCaretPosition();
        return 0;
      }
      if (wparam == kNativeWidthTimer) {
        AdvanceNativeWidth();
        return 0;
      }
      if (wparam == kNativeFadeTimer) {
        AdvanceNativeFade();
        return 0;
      }
      return DefWindowProcW(window_, message, wparam, lparam);
    case WM_NCHITTEST: {
      // A preview is display-only; allow the underlying XAML ScrollViewer to
      // receive pointer and wheel input instead of intercepting it.
      if (preview_mode_ || hide_after_fade_) {
        return HTTRANSPARENT;
      }
      POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
      ScreenToClient(window_, &point);
      const float x = static_cast<float>(point.x) / dpi_scale_ - shadow_margin_;
      const float y = static_cast<float>(point.y) / dpi_scale_ - shadow_margin_;
      if (shadow_margin_ > 0.0F && (x < 0.0F || y < 0.0F || x >= PresentedContentWidth() || y >= window_height_)) {
        return HTTRANSPARENT;
      }
      return DefWindowProcW(window_, message, wparam, lparam);
    }
    case WM_PAINT:
      Paint();
      return 0;
    case WM_SIZE:
      if (layered_rendering_enabled_) {
        const SIZE requested_size{static_cast<LONG>(LOWORD(lparam)),
                                  static_cast<LONG>(HIWORD(lparam))};
        if ((UsesNativeDefaultTheme() ? requested_size.cx > layered_pixel_size_.cx
                                     : requested_size.cx != layered_pixel_size_.cx) ||
            requested_size.cy != layered_pixel_size_.cy) {
          DiscardDeviceResources();
          InvalidateRect(window_, nullptr, FALSE);
        }
      } else if (hwnd_render_target_ != nullptr) {
        const HRESULT resize_result =
            hwnd_render_target_->Resize(D2D1::SizeU(LOWORD(lparam), HIWORD(lparam)));
        if (FAILED(resize_result)) {
          DiscardDeviceResources();
          InvalidateRect(window_, nullptr, FALSE);
        }
      }
      return 0;
    case WM_DPICHANGED: {
      KillTimer(window_, kNativeWidthTimer);
      width_active_ = false;
      const UINT dpi =
          std::max<UINT>(LOWORD(wparam), USER_DEFAULT_SCREEN_DPI);
      dpi_scale_ =
          static_cast<float>(dpi) / static_cast<float>(USER_DEFAULT_SCREEN_DPI);
      DiscardDeviceResources();
      const auto* suggested = reinterpret_cast<RECT*>(lparam);
      SetWindowPos(window_, nullptr, suggested->left, suggested->top,
                   suggested->right - suggested->left, suggested->bottom - suggested->top,
                   SWP_NOACTIVATE | SWP_NOZORDER);
      layered_present_retry_attempted_ = false;
      InvalidateRect(window_, nullptr, FALSE);
      return 0;
    }
    case WM_MOUSEMOVE: {
      if (preview_mode_) {
        return 0;
      }
      const float x = static_cast<float>(GET_X_LPARAM(lparam)) / dpi_scale_ - shadow_margin_;
      const float y = static_cast<float>(GET_Y_LPARAM(lparam)) / dpi_scale_ - shadow_margin_;
      const bool expand_hovered = can_expand_ && ContainsPoint(expand_button_bounds_, x, y);
      const bool menu_hovered = ContainsPoint(menu_button_bounds_, x, y);
      if (expand_button_hovered_ != expand_hovered || menu_button_hovered_ != menu_hovered) {
        expand_button_hovered_ = expand_hovered;
        menu_button_hovered_ = menu_hovered;
        InvalidateRect(window_, nullptr, FALSE);
      }
      if (!tracking_mouse_leave_) {
        TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, window_, 0};
        tracking_mouse_leave_ = TrackMouseEvent(&tracking) != FALSE;
      }
      return 0;
    }
    case WM_MOUSELEAVE:
      tracking_mouse_leave_ = false;
      if (expand_button_hovered_ || menu_button_hovered_) {
        expand_button_hovered_ = false;
        menu_button_hovered_ = false;
        InvalidateRect(window_, nullptr, FALSE);
      }
      return 0;
    case WM_LBUTTONDOWN: {
      if (preview_mode_) {
        return 0;
      }
      const float x = static_cast<float>(GET_X_LPARAM(lparam)) / dpi_scale_ - shadow_margin_;
      const float y = static_cast<float>(GET_Y_LPARAM(lparam)) / dpi_scale_ - shadow_margin_;
      expand_button_pressed_ = can_expand_ && ContainsPoint(expand_button_bounds_, x, y);
      menu_button_pressed_ = ContainsPoint(menu_button_bounds_, x, y);
      if (expand_button_pressed_ || menu_button_pressed_) {
        SetCapture(window_);
        InvalidateRect(window_, nullptr, FALSE);
        return 0;
      }
      return DefWindowProcW(window_, message, wparam, lparam);
    }
    case WM_LBUTTONUP: {
      if (preview_mode_) {
        return 0;
      }
      const float x = static_cast<float>(GET_X_LPARAM(lparam)) / dpi_scale_ - shadow_margin_;
      const float y = static_cast<float>(GET_Y_LPARAM(lparam)) / dpi_scale_ - shadow_margin_;
      const bool activate_expand =
          expand_button_pressed_ && can_expand_ && ContainsPoint(expand_button_bounds_, x, y);
      const bool activate_menu =
          menu_button_pressed_ && ContainsPoint(menu_button_bounds_, x, y);
      expand_button_pressed_ = false;
      menu_button_pressed_ = false;
      if (GetCapture() == window_) {
        ReleaseCapture();
      }
      InvalidateRect(window_, nullptr, FALSE);
      if (activate_expand) {
        SetExpanded(!expanded_);
        return 0;
      }
      if (activate_menu && quick_menu_action_) {
        RECT window_rectangle{};
        if (GetWindowRect(window_, &window_rectangle)) {
          const POINT anchor{
              window_rectangle.left +
                  ToPixels(shadow_margin_ + (menu_button_bounds_.left + menu_button_bounds_.right) / 2.0F,
                           dpi_scale_),
              window_rectangle.top + ToPixels(shadow_margin_ + menu_button_bounds_.top, dpi_scale_)};
          quick_menu_action_(anchor);
        }
        return 0;
      }
      return DefWindowProcW(window_, message, wparam, lparam);
    }
    case WM_CAPTURECHANGED:
      if (expand_button_pressed_ || menu_button_pressed_) {
        expand_button_pressed_ = false;
        menu_button_pressed_ = false;
        InvalidateRect(window_, nullptr, FALSE);
      }
      return 0;
    case WM_ERASEBKGND:
      return 1;
    case WM_NCDESTROY:
      StopCaretPosition();
      KillTimer(window_, kNativeFadeTimer);
      KillTimer(window_, kNativeWidthTimer);
      width_active_ = false;
      fade_active_ = false;
      window_ = nullptr;
      DiscardDeviceResources();
      return 0;
    default:
      return DefWindowProcW(window_, message, wparam, lparam);
  }
}

void CandidateWindow::RefreshTheme(std::string_view theme_id) {
  theme_manifest_ = core::MakeDefaultThemeManifest();
  theme_directory_.clear();
  if (theme_id != theme_manifest_.id) {
    const auto themes_directory = ThemesDirectoryPath();
    if (themes_directory.has_value()) {
      const auto installed = core::LoadInstalledTheme(*themes_directory, theme_id);
      if (installed.has_value()) {
        theme_manifest_ = installed->manifest;
        theme_directory_ = installed->directory;
      }
    }
  }
  theme_initialized_ = true;
}

bool CandidateWindow::UsesNativeDefaultTheme() const noexcept {
  return theme_manifest_.id == core::kDefaultThemeId || UsesMissingSsfLayoutFallback();
}

bool CandidateWindow::UsesMissingSsfLayoutFallback() const noexcept {
  if (theme_manifest_.source_format != "sogou-ssf") return false;
  const auto& appearance = dark_theme_ && theme_manifest_.dark.has_value()
      ? *theme_manifest_.dark : theme_manifest_.light;
  const auto& surface = settings_.candidate_layout == core::CandidateLayout::kHorizontal
      ? appearance.horizontal : appearance.vertical;
  return !surface.background.has_value();
}

bool CandidateWindow::UsesSogouRendering() const noexcept {
  return theme_manifest_.source_format == "sogou-ssf" && !UsesMissingSsfLayoutFallback();
}

void CandidateWindow::ApplyWindowRenderingMode() {
  if (window_ == nullptr) {
    return;
  }
  LONG_PTR extended_style = GetWindowLongPtrW(window_, GWL_EXSTYLE);
  if (layered_rendering_enabled_) {
    extended_style |= WS_EX_LAYERED;
  } else {
    extended_style &= ~static_cast<LONG_PTR>(WS_EX_LAYERED);
  }
  SetWindowLongPtrW(window_, GWL_EXSTYLE, extended_style);
  SetWindowRgn(window_, nullptr, FALSE);

  const DWM_WINDOW_CORNER_PREFERENCE preference =
      layered_rendering_enabled_
          ? DWMWCP_DONOTROUND
          : (preview_mode_ ? DWMWCP_DEFAULT : DWMWCP_ROUND);
  static_cast<void>(DwmSetWindowAttribute(
      window_, DWMWA_WINDOW_CORNER_PREFERENCE, &preference, sizeof(preference)));
  SetWindowPos(window_, nullptr, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE |
                   SWP_FRAMECHANGED);
}

const core::ThemeAppearance& CandidateWindow::ActiveThemeAppearance() const {
  if (UsesMissingSsfLayoutFallback()) {
    static const core::ThemeManifest native = core::MakeDefaultThemeManifest();
    return dark_theme_ && native.dark.has_value() ? *native.dark : native.light;
  }
  if (dark_theme_ && theme_manifest_.dark.has_value()) {
    return *theme_manifest_.dark;
  }
  return theme_manifest_.light;
}

const core::ThemeSurface& CandidateWindow::ActiveThemeSurface() const {
  const auto& appearance = ActiveThemeAppearance();
  return settings_.candidate_layout == core::CandidateLayout::kHorizontal
             ? appearance.horizontal
             : appearance.vertical;
}

CandidateWindow::RenderPalette CandidateWindow::ResolveRenderPalette() const {
  RenderPalette palette;
  if (settings_.custom_candidate_colors) {
    const auto configured = core::ResolveCandidatePalette(settings_, dark_theme_);
    palette.preedit = RgbColor(configured.preedit_color);
    palette.highlighted_candidate = RgbColor(configured.highlighted_candidate_color);
    palette.candidate = RgbColor(configured.candidate_text_color);
    palette.background = RgbColor(configured.candidate_background_color);
    palette.muted = RgbColor(configured.muted_color);
  palette.highlighted_background = RgbColor(configured.highlight_background_color);
  palette.separator = palette.muted;
  palette.surface_border =
      ArgbColor(dark_theme_ ? 0xFF5F6368U : 0xFF3A3A3AU);
    if (const auto& separator = ActiveThemeSurface().separator;
        separator.has_value() && separator->color.has_value()) {
      palette.separator = ArgbColor(*separator->color);
    }
    return palette;
  }
  const auto& configured = ActiveThemeAppearance().palette;
  palette.preedit = ArgbColor(configured.preedit_text);
  palette.highlighted_candidate = ArgbColor(configured.highlighted_candidate_text);
  palette.candidate = ArgbColor(configured.candidate_text);
  palette.background = ArgbColor(configured.background);
  palette.muted = ArgbColor(configured.muted_text);
  palette.highlighted_background = ArgbColor(configured.highlighted_background);
  palette.separator = ArgbColor(configured.separator);
  palette.surface_border =
      ArgbColor(dark_theme_ ? 0xFF5F6368U : 0xFF3A3A3AU);
  if (const auto& separator = ActiveThemeSurface().separator;
      separator.has_value() && separator->color.has_value()) {
    palette.separator = ArgbColor(*separator->color);
  }
  return palette;
}

float CandidateWindow::ThemeUnitScale() const {
  const float base_dpi =
      static_cast<float>(std::max(theme_manifest_.base_dpi, 1U));
  return static_cast<float>(USER_DEFAULT_SCREEN_DPI) / base_dpi * layout_scale_;
}

bool CandidateWindow::EnsureImagingFactory() {
  if (imaging_factory_ != nullptr) {
    return true;
  }
  return SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(imaging_factory_.ReleaseAndGetAddressOf())));
}

Microsoft::WRL::ComPtr<ID2D1Bitmap> CandidateWindow::LoadThemeBitmap(
    std::string_view asset) const {
  Microsoft::WRL::ComPtr<ID2D1Bitmap> bitmap;
  if (asset.empty() || theme_directory_.empty() || imaging_factory_ == nullptr ||
      render_target_ == nullptr) {
    return bitmap;
  }

  const std::wstring asset_name = Utf8ToWide(asset);
  if (asset_name.empty()) {
    return bitmap;
  }
  const std::filesystem::path asset_path = theme_directory_ / asset_name;
  std::error_code status_error;
  const auto status = std::filesystem::symlink_status(asset_path, status_error);
  if (status_error || !std::filesystem::is_regular_file(status) ||
      std::filesystem::is_symlink(status)) {
    return bitmap;
  }

  Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
  if (FAILED(imaging_factory_->CreateDecoderFromFilename(
          asset_path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad,
          decoder.GetAddressOf()))) {
    return bitmap;
  }
  Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
  if (FAILED(decoder->GetFrame(0, frame.GetAddressOf()))) {
    return bitmap;
  }
  Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
  if (FAILED(imaging_factory_->CreateFormatConverter(converter.GetAddressOf())) ||
      FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA,
                                   WICBitmapDitherTypeNone, nullptr, 0.0,
                                   WICBitmapPaletteTypeCustom))) {
    return bitmap;
  }
  Microsoft::WRL::ComPtr<IWICBitmap> detached;
  if (FAILED(imaging_factory_->CreateBitmapFromSource(
          converter.Get(), WICBitmapCacheOnLoad, detached.GetAddressOf()))) {
    return bitmap;
  }

  const float bitmap_dpi =
      static_cast<float>(std::max(theme_manifest_.base_dpi, 1U));
  const D2D1_BITMAP_PROPERTIES properties = D2D1::BitmapProperties(
      D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                        D2D1_ALPHA_MODE_PREMULTIPLIED),
      bitmap_dpi, bitmap_dpi);
  if (FAILED(render_target_->CreateBitmapFromWicBitmap(
          detached.Get(), &properties, bitmap.GetAddressOf()))) {
    bitmap.Reset();
  }
  return bitmap;
}

CandidateWindow::ButtonBitmaps CandidateWindow::LoadButtonBitmaps(
    const std::optional<core::ThemeButtonImages>& images) const {
  ButtonBitmaps bitmaps;
  if (!images.has_value()) {
    return bitmaps;
  }
  bitmaps.normal = LoadThemeBitmap(images->normal);
  bitmaps.hover =
      images->hover.empty() ? bitmaps.normal : LoadThemeBitmap(images->hover);
  bitmaps.pressed =
      images->pressed.empty() ? bitmaps.normal : LoadThemeBitmap(images->pressed);
  if (bitmaps.hover == nullptr) {
    bitmaps.hover = bitmaps.normal;
  }
  if (bitmaps.pressed == nullptr) {
    bitmaps.pressed = bitmaps.normal;
  }
  return bitmaps;
}

void CandidateWindow::LoadSurfaceBitmaps() {
  surface_bitmaps_ = {};
  if (!EnsureImagingFactory()) {
    return;
  }
  const auto& surface = ActiveThemeSurface();
  if (surface.background.has_value()) {
    surface_bitmaps_.background = LoadThemeBitmap(surface.background->asset);
  }
  std::vector<core::ThemeOverlay> overlays = surface.overlays;
  std::stable_sort(overlays.begin(), overlays.end(),
                   [](const core::ThemeOverlay& first,
                      const core::ThemeOverlay& second) {
                     if (first.draw_order != second.draw_order) {
                       return first.draw_order < second.draw_order;
                     }
                     return first.custom_index < second.custom_index;
                   });
  surface_bitmaps_.overlays.reserve(overlays.size());
  for (const auto& overlay : overlays) {
    auto bitmap = LoadThemeBitmap(overlay.asset);
    if (bitmap != nullptr) {
      surface_bitmaps_.overlays.push_back(
          SurfaceBitmaps::OverlayBitmap{overlay, std::move(bitmap)});
    }
  }
  if (surface.separator.has_value()) {
    surface_bitmaps_.separator = LoadThemeBitmap(surface.separator->asset);
  }
  surface_bitmaps_.previous = LoadButtonBitmaps(surface.previous_button);
  surface_bitmaps_.next = LoadButtonBitmaps(surface.next_button);
  surface_bitmaps_.expand = LoadButtonBitmaps(surface.expand_button);
  surface_bitmaps_.collapse = LoadButtonBitmaps(surface.collapse_button);
  surface_bitmaps_.menu = LoadButtonBitmaps(surface.menu_button);
}

bool CandidateWindow::EnsureDeviceResources() {
  if (render_target_ != nullptr) {
    return true;
  }

  RECT client{};
  GetClientRect(window_, &client);
  const UINT32 width =
      static_cast<UINT32>(std::max({client.right - client.left, 1L,
          width_active_ ? width_to_rectangle_.right - width_to_rectangle_.left : 1L}));
  const UINT32 height =
      static_cast<UINT32>(std::max(client.bottom - client.top, 1L));
  const D2D1_SIZE_U size = D2D1::SizeU(width, height);
  if (layered_rendering_enabled_) {
    if (!EnsureLayeredSurface(width, height)) {
      return false;
    }
    render_target_ = dc_render_target_;
  } else {
    if (FAILED(d2d_factory_->CreateHwndRenderTarget(
            D2D1::RenderTargetProperties(),
            D2D1::HwndRenderTargetProperties(window_, size),
            hwnd_render_target_.ReleaseAndGetAddressOf()))) {
      return false;
    }
    render_target_ = hwnd_render_target_;
  }

  const RenderPalette palette = ResolveRenderPalette();

  if (FAILED(render_target_->CreateSolidColorBrush(palette.candidate,
                                                   text_brush_.ReleaseAndGetAddressOf())) ||
      FAILED(render_target_->CreateSolidColorBrush(palette.preedit,
                                                   preedit_brush_.ReleaseAndGetAddressOf())) ||
      FAILED(render_target_->CreateSolidColorBrush(
          palette.highlighted_candidate,
          highlighted_text_brush_.ReleaseAndGetAddressOf())) ||
      FAILED(render_target_->CreateSolidColorBrush(palette.muted,
                                                   muted_brush_.ReleaseAndGetAddressOf())) ||
      FAILED(render_target_->CreateSolidColorBrush(
          palette.highlighted_background, accent_brush_.ReleaseAndGetAddressOf())) ||
      FAILED(render_target_->CreateSolidColorBrush(
          palette.separator, separator_brush_.ReleaseAndGetAddressOf())) ||
      FAILED(render_target_->CreateSolidColorBrush(
          palette.background, background_brush_.ReleaseAndGetAddressOf())) ||
      FAILED(render_target_->CreateSolidColorBrush(
          palette.surface_border,
          surface_border_brush_.ReleaseAndGetAddressOf()))) {
    DiscardDeviceResources();
    return false;
  }

  const auto& theme_typography = ActiveThemeAppearance().typography;
  std::wstring chinese_font_family = Utf8ToWide(theme_typography.chinese_font_family);
  std::wstring english_font_family = Utf8ToWide(theme_typography.english_font_family);
  if (chinese_font_family.empty()) {
    chinese_font_family = L"Source Han Sans SC";
  }
  if (english_font_family.empty()) {
    english_font_family = L"Segoe UI Variable Text";
  }
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
          : std::clamp<std::uint32_t>(
                theme_typography.font_size,
                static_cast<std::uint32_t>(core::kMinimumCandidateFontSize),
                static_cast<std::uint32_t>(core::kMaximumCandidateFontSize)));
  const DWRITE_FONT_WEIGHT preedit_weight =
      UsesSogouRendering() ? DWRITE_FONT_WEIGHT_NORMAL
                           : DWRITE_FONT_WEIGHT_SEMI_BOLD;
  const float preedit_font_size =
      UsesSogouRendering()
          ? detail::ResolveSogouPreeditDWriteFontSize(font_size)
          : font_size + 1.0F;
  if (FAILED(dwrite_factory_->CreateTextFormat(
          english_font_family.c_str(), nullptr, preedit_weight,
          DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, preedit_font_size, L"zh-CN",
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
  LoadSurfaceBitmaps();
  return true;
}

bool CandidateWindow::EnsureLayeredSurface(UINT32 width, UINT32 height) {
  if (width == 0 || height == 0 ||
      width > static_cast<UINT32>(std::numeric_limits<LONG>::max()) ||
      height > static_cast<UINT32>(std::numeric_limits<LONG>::max())) {
    return false;
  }
  if (dc_render_target_ != nullptr && layered_memory_dc_ != nullptr &&
      layered_bitmap_ != nullptr &&
      layered_pixel_size_.cx == static_cast<LONG>(width) &&
      layered_pixel_size_.cy == static_cast<LONG>(height)) {
    const RECT bounds{0, 0, static_cast<LONG>(width),
                      static_cast<LONG>(height)};
    return SUCCEEDED(dc_render_target_->BindDC(layered_memory_dc_, &bounds));
  }

  ReleaseLayeredSurface();
  layered_memory_dc_ = CreateCompatibleDC(nullptr);
  if (layered_memory_dc_ == nullptr) {
    return false;
  }

  BITMAPINFO bitmap_info{};
  bitmap_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bitmap_info.bmiHeader.biWidth = static_cast<LONG>(width);
  bitmap_info.bmiHeader.biHeight = -static_cast<LONG>(height);
  bitmap_info.bmiHeader.biPlanes = 1;
  bitmap_info.bmiHeader.biBitCount = 32;
  bitmap_info.bmiHeader.biCompression = BI_RGB;
  void* pixels = nullptr;
  layered_bitmap_ =
      CreateDIBSection(layered_memory_dc_, &bitmap_info, DIB_RGB_COLORS,
                       &pixels, nullptr, 0);
  if (layered_bitmap_ == nullptr || pixels == nullptr) {
    ReleaseLayeredSurface();
    return false;
  }
  layered_previous_bitmap_ = SelectObject(layered_memory_dc_, layered_bitmap_);
  if (layered_previous_bitmap_ == nullptr ||
      layered_previous_bitmap_ == HGDI_ERROR) {
    layered_previous_bitmap_ = nullptr;
    ReleaseLayeredSurface();
    return false;
  }

  const float dpi =
      dpi_scale_ * static_cast<float>(USER_DEFAULT_SCREEN_DPI);
  const D2D1_RENDER_TARGET_PROPERTIES properties = D2D1::RenderTargetProperties(
      D2D1_RENDER_TARGET_TYPE_DEFAULT,
      D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                        D2D1_ALPHA_MODE_PREMULTIPLIED),
      dpi, dpi, D2D1_RENDER_TARGET_USAGE_NONE,
      D2D1_FEATURE_LEVEL_DEFAULT);
  if (FAILED(d2d_factory_->CreateDCRenderTarget(
          &properties, dc_render_target_.ReleaseAndGetAddressOf()))) {
    ReleaseLayeredSurface();
    return false;
  }
  const RECT bounds{0, 0, static_cast<LONG>(width),
                    static_cast<LONG>(height)};
  if (FAILED(dc_render_target_->BindDC(layered_memory_dc_, &bounds))) {
    ReleaseLayeredSurface();
    return false;
  }
  layered_pixel_size_ =
      SIZE{static_cast<LONG>(width), static_cast<LONG>(height)};
  return true;
}

void CandidateWindow::ReleaseLayeredSurface() {
  render_target_.Reset();
  dc_render_target_.Reset();
  if (layered_memory_dc_ != nullptr && layered_previous_bitmap_ != nullptr) {
    static_cast<void>(
        SelectObject(layered_memory_dc_, layered_previous_bitmap_));
  }
  layered_previous_bitmap_ = nullptr;
  if (layered_bitmap_ != nullptr) {
    DeleteObject(layered_bitmap_);
    layered_bitmap_ = nullptr;
  }
  if (layered_memory_dc_ != nullptr) {
    DeleteDC(layered_memory_dc_);
    layered_memory_dc_ = nullptr;
  }
  layered_pixel_size_ = {};
}

bool CandidateWindow::PresentLayeredSurface() {
  if (!layered_rendering_enabled_ || window_ == nullptr ||
      layered_memory_dc_ == nullptr || layered_pixel_size_.cx <= 0 ||
      layered_pixel_size_.cy <= 0) {
    return false;
  }
  RECT window_rectangle{};
  if (!GetWindowRect(window_, &window_rectangle)) {
    return false;
  }
  HDC screen_dc = GetDC(nullptr);
  if (screen_dc == nullptr) {
    return false;
  }
  POINT destination{window_rectangle.left, window_rectangle.top};
  POINT source{};
  SIZE presentation_size = layered_pixel_size_;
  if (UsesNativeDefaultTheme()) {
    // The backing bitmap can be wider during a resize. Present only the current
    // physical width; otherwise UpdateLayeredWindow snaps back to the bitmap size.
    presentation_size.cx = std::min(presentation_size.cx,
                                    window_rectangle.right - window_rectangle.left);
  }
  BLENDFUNCTION blend{AC_SRC_OVER, 0,
                      static_cast<BYTE>(std::lround(std::clamp(surface_opacity_, 0.0F, 1.0F) * 255.0F)),
                      AC_SRC_ALPHA};
  const BOOL presented = UpdateLayeredWindow(
      window_, screen_dc, preview_mode_ ? nullptr : &destination, &presentation_size,
      layered_memory_dc_, &source, 0, &blend, ULW_ALPHA);
  ReleaseDC(nullptr, screen_dc);
  return presented != FALSE;
}

void CandidateWindow::DrawSurfaceBackground() {
  if (render_target_ == nullptr) {
    return;
  }

  const auto& surface = ActiveThemeSurface();
  render_target_->Clear(layered_rendering_enabled_
                            ? D2D1::ColorF(0.0F, 0.0F, 0.0F, 0.0F)
                             : ResolveRenderPalette().background);
  if (shadow_margin_ > 0.0F) {
    detail::DrawNativeCandidateShadow(
        d2d_factory_.Get(), render_target_.Get(),
        D2D1::RectF(shadow_margin_, shadow_margin_, shadow_margin_ + PresentedContentWidth(),
                    shadow_margin_ + window_height_),
        12.0F * layout_scale_, layout_scale_, dark_theme_);
    render_target_->SetTransform(D2D1::Matrix3x2F::Translation(shadow_margin_, shadow_margin_));
  }
  const auto draw_palette_surface = [this]() {
    if (background_brush_ == nullptr || surface_border_brush_ == nullptr) {
      return;
    }
    const D2D1_SIZE_F target_size = UsesNativeDefaultTheme()
        ? D2D1::SizeF(PresentedContentWidth(), window_height_) : render_target_->GetSize();
    if (target_size.width <= 0.0F || target_size.height <= 0.0F) {
      return;
    }
    const D2D1_RECT_F outer_bounds =
        D2D1::RectF(0.0F, 0.0F, target_size.width, target_size.height);
    const bool native_default = UsesNativeDefaultTheme();
    const float corner_radius = native_default ? 12.0F * layout_scale_ : kSurfaceCornerRadius;
    FillCandidateRoundedRect(d2d_factory_.Get(), render_target_.Get(), outer_bounds,
                             corner_radius, surface_border_brush_.Get(), native_default);

    const float border_width =
        std::ceil(std::max(dpi_scale_, 1.0F)) /
        std::max(dpi_scale_, 1.0F);
    const D2D1_RECT_F inner_bounds =
        D2D1::RectF(border_width, border_width,
                    std::max(border_width, target_size.width - border_width),
                    std::max(border_width, target_size.height - border_width));
    const float inner_radius =
        std::max(corner_radius - border_width, 0.0F);
    FillCandidateRoundedRect(d2d_factory_.Get(), render_target_.Get(), inner_bounds,
                             inner_radius, background_brush_.Get(), native_default);
  };
  if (!surface.background.has_value()) {
    draw_palette_surface();
    return;
  }

  if (surface_bitmaps_.background == nullptr) {
    draw_palette_surface();
    return;
  }

  const D2D1_SIZE_F bitmap_size = surface_bitmaps_.background->GetSize();
  const D2D1_SIZE_F target_size = render_target_->GetSize();
  if (bitmap_size.width <= 0.0F || bitmap_size.height <= 0.0F ||
      target_size.width <= 0.0F || target_size.height <= 0.0F) {
    draw_palette_surface();
    return;
  }

  const float source_unit =
      static_cast<float>(USER_DEFAULT_SCREEN_DPI) /
      static_cast<float>(std::max(theme_manifest_.base_dpi, 1U));
  float source_left =
      std::min(static_cast<float>(surface.background->stretch.left) * source_unit,
               bitmap_size.width);
  float source_right =
      std::min(static_cast<float>(surface.background->stretch.right) * source_unit,
               bitmap_size.width);
  float source_top =
      std::min(static_cast<float>(surface.background->stretch.top) * source_unit,
               bitmap_size.height);
  float source_bottom =
      std::min(static_cast<float>(surface.background->stretch.bottom) * source_unit,
               bitmap_size.height);
  if (UsesSogouRendering() &&
      settings_.candidate_layout == core::CandidateLayout::kVertical &&
      source_left + source_right >= bitmap_size.width && source_left >= 1.0F &&
      source_left < bitmap_size.width) {
    // Overlapping horizontal V1 cuts retain the right-hand artwork unchanged.
    // Collapse the overlap to a one-source-pixel seam rather than rejecting
    // the entire asset or scaling its fixed edges.
    source_right = bitmap_size.width - source_left;
    source_left -= 1.0F;
  }
  // Custom H1 references with exhausted/overlapping vertical slices retain
  // every source row at the authored height. Those vertical cuts must not
  // discard the horizontally expandable image. Keep this path limited to
  // native SSF typography; override/vertical-layout behavior is not inferred.
  if (UsesSogouRendering() &&
      settings_.candidate_layout == core::CandidateLayout::kHorizontal &&
      !settings_.custom_candidate_font_size && !settings_.custom_candidate_fonts &&
      source_left + source_right < bitmap_size.width &&
      source_top + source_bottom >= bitmap_size.height &&
      target_size.height >= bitmap_size.height * layout_scale_ &&
      target_size.width >= (source_left + source_right) * layout_scale_) {
    const std::array<float, 4> source_x{
        0.0F, source_left, bitmap_size.width - source_right, bitmap_size.width};
    const std::array<float, 4> destination_x{
        0.0F, source_left * layout_scale_,
        target_size.width - source_right * layout_scale_, target_size.width};
    for (std::size_t column = 0; column < 3; ++column) {
      detail::DrawSogouBitmapPatch(render_target_.Get(), surface_bitmaps_.background.Get(),
          D2D1::RectF(source_x[column], 0.0F, source_x[column + 1], bitmap_size.height),
          D2D1::RectF(destination_x[column], 0.0F, destination_x[column + 1],
                      bitmap_size.height * layout_scale_),
          column == 1 ? surface.background->horizontal_layout
                      : core::ThemeImageLayout::kStretch,
          core::ThemeImageLayout::kStretch, layout_scale_);
    }
    return;
  }
  // A general nine-slice needs a non-empty center source region. Malformed or
  // incompatible margins otherwise leave a transparent hole in a wider
  // candidate window, so retain the palette background as the documented
  // native fallback.
  if (source_left + source_right >= bitmap_size.width ||
      source_top + source_bottom >= bitmap_size.height) {
    draw_palette_surface();
    return;
  }
  const auto fit_pair = [](float* first, float* second, float available) {
    const float total = *first + *second;
    if (total > available && total > 0.0F) {
      const float scale = available / total;
      *first *= scale;
      *second *= scale;
    }
  };
  float destination_left = source_left * layout_scale_;
  float destination_right = source_right * layout_scale_;
  float destination_top = source_top * layout_scale_;
  float destination_bottom = source_bottom * layout_scale_;
  fit_pair(&destination_left, &destination_right, target_size.width);
  fit_pair(&destination_top, &destination_bottom, target_size.height);

  const std::array<float, 4> source_x{
      0.0F, source_left, bitmap_size.width - source_right, bitmap_size.width};
  const std::array<float, 4> source_y{
      0.0F, source_top, bitmap_size.height - source_bottom, bitmap_size.height};
  const std::array<float, 4> destination_x{
      0.0F, destination_left, target_size.width - destination_right,
      target_size.width};
  const std::array<float, 4> destination_y{
      0.0F, destination_top, target_size.height - destination_bottom,
      target_size.height};

  for (std::size_t row = 0; row < 3; ++row) {
    for (std::size_t column = 0; column < 3; ++column) {
      const D2D1_RECT_F source =
          D2D1::RectF(source_x[column], source_y[row], source_x[column + 1],
                      source_y[row + 1]);
      const D2D1_RECT_F destination =
          D2D1::RectF(destination_x[column], destination_y[row],
                      destination_x[column + 1], destination_y[row + 1]);
      const bool tile_horizontal =
          column == 1 &&
          surface.background->horizontal_layout == core::ThemeImageLayout::kTile;
      const bool tile_vertical =
          row == 1 &&
          surface.background->vertical_layout == core::ThemeImageLayout::kTile;
      const core::ThemeImageLayout horizontal_layout =
          column == 1 ? surface.background->horizontal_layout
                      : core::ThemeImageLayout::kStretch;
      const core::ThemeImageLayout vertical_layout =
          row == 1 ? surface.background->vertical_layout
                   : core::ThemeImageLayout::kStretch;
      detail::DrawSogouBitmapPatch(render_target_.Get(), surface_bitmaps_.background.Get(), source,
                      destination,
                      tile_horizontal ? core::ThemeImageLayout::kTile
                                      : horizontal_layout,
                      tile_vertical ? core::ThemeImageLayout::kTile
                                    : vertical_layout,
                      layout_scale_);
    }
  }
}

void CandidateWindow::DrawSurfaceOverlays() {
  if (render_target_ == nullptr) {
    return;
  }
  const D2D1_SIZE_F surface_size = render_target_->GetSize();
  for (const auto& overlay : surface_bitmaps_.overlays) {
    if (overlay.bitmap == nullptr) {
      continue;
    }
    const D2D1_SIZE_F source_size = overlay.bitmap->GetSize();
    if (source_size.width <= 0.0F || source_size.height <= 0.0F) {
      continue;
    }
    const D2D1_RECT_F source =
        D2D1::RectF(0.0F, 0.0F, source_size.width, source_size.height);
    const D2D1_RECT_F destination = DecodeSogouOverlayBounds(
        overlay.overlay, source_size, surface_size, layout_scale_);
    render_target_->DrawBitmap(overlay.bitmap.Get(), destination, 1.0F,
                               D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,
                               source);
  }
}

void CandidateWindow::DrawSurfaceSeparator(float y) {
  if (render_target_ == nullptr) {
    return;
  }
  const auto& separator = ActiveThemeSurface().separator;
  if (!separator.has_value()) {
    if (UsesSogouRendering()) {
      return;
    }
    render_target_->DrawLine(
        D2D1::Point2F((UsesNativeDefaultTheme() ? 10.0F : kHorizontalPadding) * layout_scale_, y),
        D2D1::Point2F(PresentedContentWidth() - (UsesNativeDefaultTheme() ? 10.0F : kHorizontalPadding) * layout_scale_, y),
        muted_brush_.Get(), 0.5F);
    return;
  }

  const float unit_scale = ThemeUnitScale();
  const float left = std::min(static_cast<float>(separator->left) * unit_scale,
                              window_width_);
  const float right =
      std::max(left, window_width_ -
                         static_cast<float>(separator->right) * unit_scale);
  const float thickness =
      std::max(static_cast<float>(separator->thickness) * unit_scale, 0.5F);
  if (surface_bitmaps_.separator != nullptr) {
    const D2D1_SIZE_F bitmap_size = surface_bitmaps_.separator->GetSize();
    const D2D1_RECT_F source =
        D2D1::RectF(0.0F, 0.0F, bitmap_size.width, bitmap_size.height);
    const D2D1_RECT_F destination =
        D2D1::RectF(left, y - thickness / 2.0F, right, y + thickness / 2.0F);
    detail::DrawSogouBitmapPatch(render_target_.Get(), surface_bitmaps_.separator.Get(), source,
                    destination, core::ThemeImageLayout::kTile,
                    core::ThemeImageLayout::kStretch, layout_scale_);
    return;
  }
  // The active H1 use_gdip path does not paint the legacy color-only
  // separator field. Keep explicit separator images and other paths intact.
  if (UsesSogouRendering() &&
      ActiveThemeAppearance().typography.sogou_use_gdip == 1U && separator->asset.empty()) {
    return;
  }
  render_target_->DrawLine(D2D1::Point2F(left, y), D2D1::Point2F(right, y),
                           separator_brush_.Get(), thickness);
}

bool CandidateWindow::DrawThemeButton(const ButtonBitmaps& bitmaps,
                                      const D2D1_RECT_F& bounds, bool hovered,
                                      bool pressed) {
  ID2D1Bitmap* bitmap = nullptr;
  if (pressed && bitmaps.pressed != nullptr) {
    bitmap = bitmaps.pressed.Get();
  } else if (hovered && bitmaps.hover != nullptr) {
    bitmap = bitmaps.hover.Get();
  } else {
    bitmap = bitmaps.normal.Get();
  }
  if (bitmap == nullptr || render_target_ == nullptr) {
    return false;
  }

  const D2D1_SIZE_F source_size = bitmap->GetSize();
  const float bounds_width = bounds.right - bounds.left;
  const float bounds_height = bounds.bottom - bounds.top;
  if (source_size.width <= 0.0F || source_size.height <= 0.0F ||
      bounds_width <= 0.0F || bounds_height <= 0.0F) {
    return false;
  }
  const float desired_width = source_size.width * layout_scale_;
  const float desired_height = source_size.height * layout_scale_;
  const float fit_scale =
      std::min({1.0F, bounds_width / desired_width, bounds_height / desired_height});
  const float width = desired_width * fit_scale;
  const float height = desired_height * fit_scale;
  const float center_x = (bounds.left + bounds.right) / 2.0F;
  const float center_y = (bounds.top + bounds.bottom) / 2.0F;
  const D2D1_RECT_F destination =
      D2D1::RectF(center_x - width / 2.0F, center_y - height / 2.0F,
                  center_x + width / 2.0F, center_y + height / 2.0F);
  render_target_->DrawBitmap(bitmap, destination, 1.0F,
                             D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
  return true;
}

void CandidateWindow::Paint() {
  PAINTSTRUCT paint{};
  BeginPaint(window_, &paint);
  if (hide_after_fade_ && layered_memory_dc_ != nullptr) {
    static_cast<void>(PresentLayeredSurface());
    EndPaint(window_, &paint);
    return;
  }

  if (EnsureDeviceResources()) {
    render_target_->BeginDraw();
    render_target_->SetTransform(D2D1::Matrix3x2F::Identity());
    DrawSurfaceBackground();
    if (shadow_margin_ > 0.0F) {
      render_target_->PushAxisAlignedClip(D2D1::RectF(0, 0, PresentedContentWidth(), window_height_),
                                          D2D1_ANTIALIAS_MODE_ALIASED);
    }
    DrawSurfaceOverlays();

    const bool horizontal = settings_.candidate_layout == core::CandidateLayout::kHorizontal;
    const bool native_default = UsesNativeDefaultTheme();
    const bool compact = native_default && horizontal;
    const bool ssf_gdi = UsesSogouRendering() &&
        ActiveThemeAppearance().typography.sogou_use_gdip == 1U &&
        !settings_.custom_candidate_fonts && !settings_.custom_candidate_font_size;
    const auto page_window = core::MakeCandidatePageWindow(
        snapshot_.candidates.size(), settings_.candidate_count, page_offset_, expanded_);
    const auto slice = page_window.visible;
    const float preedit_bottom = preedit_height_;
    float ssf_preedit_width = 0.0F;
    float ssf_preedit_height = 0.0F;
    bool drew_ssf_preedit = false;
    if (native_default && native_preedit_layout_ != nullptr) {
      // Clip the enclosing ink box, not the font em box. The layout origin is
      // offset so ascenders, descenders and antialias coverage stay inside it.
      render_target_->PushAxisAlignedClip(
          D2D1::RectF(preedit_insets_.left, preedit_insets_.top,
                      window_width_ - preedit_insets_.right,
                      preedit_bottom - preedit_insets_.bottom),
          D2D1_ANTIALIAS_MODE_ALIASED);
      render_target_->DrawTextLayout(
          D2D1::Point2F(preedit_insets_.left + native_preedit_origin_offset_.x,
                        preedit_insets_.top + native_preedit_origin_offset_.y),
          native_preedit_layout_.Get(), preedit_brush_.Get());
      render_target_->PopAxisAlignedClip();
    } else if (ssf_gdi && (drew_ssf_preedit = DrawSsfGdiText(render_target_.Get(), preedit_format_.Get(),
        snapshot_.preedit, candidate_format_->GetFontSize(),
        D2D1::RectF(preedit_insets_.left, preedit_insets_.top,
                    window_width_ - preedit_insets_.right,
                    std::max(preedit_insets_.top, preedit_bottom - preedit_insets_.bottom)),
        preedit_brush_->GetColor(), {}, &ssf_preedit_width,
        ActiveThemeAppearance().typography.english_font_family !=
            ActiveThemeAppearance().typography.chinese_font_family, &ssf_preedit_height))) {
      // The source mask is already premultiplied before layered composition.
    } else {
      render_target_->DrawTextW(
        snapshot_.preedit.c_str(), static_cast<UINT32>(snapshot_.preedit.size()),
        preedit_format_.Get(),
        D2D1::RectF(preedit_insets_.left, preedit_insets_.top,
                    window_width_ - preedit_insets_.right,
                    std::max(preedit_insets_.top,
                             preedit_bottom - preedit_insets_.bottom)),
        preedit_brush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }
    if (drew_ssf_preedit) {
      const float caret_x = std::round(preedit_insets_.left + ssf_preedit_width);
      render_target_->FillRectangle(D2D1::RectF(caret_x, preedit_insets_.top,
          caret_x + 1.0F, preedit_insets_.top + ssf_preedit_height),
          highlighted_text_brush_.Get());
    }
    if (slice.count != 0) {
      DrawSurfaceSeparator(preedit_bottom);
    }
    for (std::size_t visible_index = 0; visible_index < slice.count; ++visible_index) {
      const std::size_t candidate_index = slice.offset + visible_index;
      const float cell_width = horizontal && visible_index < candidate_widths_.size()
                                   ? candidate_widths_[visible_index]
                                   : window_width_ - candidate_insets_.left -
                                         candidate_insets_.right;
      const float left = horizontal && visible_index < candidate_lefts_.size()
                             ? candidate_lefts_[visible_index]
                             : candidate_insets_.left;
      const float top = horizontal && visible_index < candidate_tops_.size()
                            ? candidate_tops_[visible_index]
                            : preedit_height_ + candidate_insets_.top +
                                  static_cast<float>(visible_index) *
                                      candidate_row_height_;
      const float right = horizontal ? left + cell_width - 2.0F * layout_scale_
                                     : window_width_ - candidate_insets_.right;
      const float row_height = candidate_row_height_;
      const D2D1_RECT_F row =
          D2D1::RectF(left, top + 1.0F, right, top + row_height - 1.0F);
      if (candidate_index == snapshot_.highlighted_index) {
        FillCandidateRoundedRect(d2d_factory_.Get(), render_target_.Get(), row,
                                 native_default ? 8.0F * layout_scale_ : kCornerRadius,
                                 accent_brush_.Get(), native_default);
      }

      const bool active = candidate_index >= page_window.active.offset &&
                          candidate_index < page_window.active.offset + page_window.active.count;
      const std::wstring label =
          active
              ? std::to_wstring(candidate_index - page_window.active.offset + 1) +
                    (UsesSogouRendering() ? L"." : (compact ? L" " : L"  ")) +
                    snapshot_.candidates[candidate_index].text
              : snapshot_.candidates[candidate_index].text;
      const auto& annotation = snapshot_.candidates[candidate_index].annotation;
      const float vertical_annotation_left =
          annotation.empty()
              ? window_width_ - candidate_insets_.right
              : std::max(candidate_insets_.left + 180.0F * layout_scale_,
                         window_width_ - candidate_insets_.right -
                             120.0F * layout_scale_);
      const std::wstring ssf_prefix = active
          ? std::to_wstring(candidate_index - page_window.active.offset + 1) + L"." : L"";
      if (!(ssf_gdi && DrawSsfGdiText(render_target_.Get(), candidate_format_.Get(),
          horizontal ? snapshot_.candidates[candidate_index].text : label,
          candidate_format_->GetFontSize(), D2D1::RectF(left, top, right, top + row_height),
          candidate_index == snapshot_.highlighted_index
              ? highlighted_text_brush_->GetColor() : text_brush_->GetColor(),
          horizontal ? ssf_prefix : std::wstring{}))) {
      render_target_->DrawTextW(label.c_str(), static_cast<UINT32>(label.size()),
                                candidate_format_.Get(),
                                D2D1::RectF(horizontal ? left +
                                    (UsesSogouRendering() ? 0.0F : (compact ? 6.0F : 8.0F)) * layout_scale_
                                                       : candidate_insets_.left,
                                            top,
                                            horizontal ? right - 6.0F * layout_scale_
                                                       : vertical_annotation_left -
                                                             10.0F * layout_scale_,
                                            top + row_height),
                                candidate_index == snapshot_.highlighted_index
                                    ? highlighted_text_brush_.Get()
                                    : text_brush_.Get(),
                                D2D1_DRAW_TEXT_OPTIONS_CLIP);

      }

      if (!horizontal && !annotation.empty()) {
        render_target_->DrawTextW(
            annotation.c_str(), static_cast<UINT32>(annotation.size()), annotation_format_.Get(),
            D2D1::RectF(vertical_annotation_left, top + 4.0F * layout_scale_,
                        window_width_ - candidate_insets_.right,
                        top + candidate_row_height_),
            muted_brush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
      }
    }

    if (horizontal && slice.count != 0) {
      bool drew_expand_image = false;
      if (can_expand_) {
        const ButtonBitmaps& expand_bitmaps =
            expanded_ ? surface_bitmaps_.collapse : surface_bitmaps_.expand;
        drew_expand_image =
            DrawThemeButton(expand_bitmaps, expand_button_bounds_,
                            expand_button_hovered_, expand_button_pressed_);
      }
      const bool drew_menu_image =
          DrawThemeButton(surface_bitmaps_.menu, menu_button_bounds_,
                          menu_button_hovered_, menu_button_pressed_);

      if (can_expand_ && !drew_expand_image && !UsesSogouRendering()) {
        render_target_->DrawLine(
            D2D1::Point2F(expand_button_bounds_.left,
                          expand_button_bounds_.top + 4.0F * layout_scale_),
            D2D1::Point2F(expand_button_bounds_.left,
                          expand_button_bounds_.bottom - 4.0F * layout_scale_),
            muted_brush_.Get(), 0.5F);
        const float center_x =
            (expand_button_bounds_.left + expand_button_bounds_.right) / 2.0F;
        const float center_y =
            (expand_button_bounds_.top + expand_button_bounds_.bottom) / 2.0F;
        const float direction = expanded_ ? -1.0F : 1.0F;
        render_target_->DrawLine(
            D2D1::Point2F(center_x - 6.0F * layout_scale_,
                          center_y - direction * 3.0F * layout_scale_),
            D2D1::Point2F(center_x, center_y + direction * 3.0F * layout_scale_),
            text_brush_.Get(), 1.6F * layout_scale_);
        render_target_->DrawLine(
            D2D1::Point2F(center_x, center_y + direction * 3.0F * layout_scale_),
            D2D1::Point2F(center_x + 6.0F * layout_scale_,
                          center_y - direction * 3.0F * layout_scale_),
            text_brush_.Get(), 1.6F * layout_scale_);
      }

      if (!drew_menu_image && ssf_gdi && menu_button_bounds_.right > menu_button_bounds_.left) {
        const float center_x = (menu_button_bounds_.left + menu_button_bounds_.right) * 0.5F;
        const float center_y = (menu_button_bounds_.top + menu_button_bounds_.bottom) * 0.5F;
        for (const float offset : {-4.0F, 0.0F, 4.0F}) {
          render_target_->DrawLine(D2D1::Point2F(center_x - 8.0F, center_y + offset),
              D2D1::Point2F(center_x + 8.0F, center_y + offset), text_brush_.Get(), 1.2F);
        }
      }
      if (!drew_menu_image && !UsesSogouRendering()) {
        render_target_->DrawLine(
            D2D1::Point2F(menu_button_bounds_.left,
                          menu_button_bounds_.top + 4.0F * layout_scale_),
            D2D1::Point2F(menu_button_bounds_.left,
                          menu_button_bounds_.bottom - 4.0F * layout_scale_),
            muted_brush_.Get(), 0.5F);
        const float menu_center_x =
            (menu_button_bounds_.left + menu_button_bounds_.right) / 2.0F;
        const float menu_center_y =
            (menu_button_bounds_.top + menu_button_bounds_.bottom) / 2.0F;
        for (const float offset : {-6.0F, 0.0F, 6.0F}) {
          render_target_->DrawLine(
              D2D1::Point2F(menu_center_x - (compact ? 7.0F : 10.0F) * layout_scale_,
                            menu_center_y + offset * layout_scale_),
              D2D1::Point2F(menu_center_x + (compact ? 7.0F : 10.0F) * layout_scale_,
                            menu_center_y + offset * layout_scale_),
              text_brush_.Get(), 1.4F * layout_scale_);
        }
      }
    }

    if (!horizontal && ssf_gdi && slice.count != 0) {
      const float rows_bottom = preedit_height_ + candidate_insets_.top +
          candidate_row_height_ * static_cast<float>(slice.count);
      const auto pager = detail::ResolveSogouVerticalPagerLayout(
          candidate_insets_.left, rows_bottom, 1.0F);
      const auto arrow = [&](const detail::SogouHorizontalBounds& b, bool next, bool enabled) {
        const float old_opacity = text_brush_->GetOpacity();
        text_brush_->SetOpacity(enabled ? 1.0F : detail::kSogouVerticalPagerDisabledOpacity);
        const float mid = (b.top + b.bottom) * 0.5F;
        for (int column = 0; column < 6; ++column) {
          const float extent = static_cast<float>(next ? 5 - column : column);
          const float x = b.left + static_cast<float>(column);
          render_target_->DrawLine(D2D1::Point2F(x, mid - extent),
              D2D1::Point2F(x, mid + extent), text_brush_.Get(), 1.0F);
        }
        text_brush_->SetOpacity(old_opacity);
      };
      arrow(pager.previous_icon, false, snapshot_.has_previous_page);
      arrow(pager.next_icon, true, snapshot_.has_next_page);
      const float cx = (menu_button_bounds_.left + menu_button_bounds_.right) * 0.5F;
      const float cy = (menu_button_bounds_.top + menu_button_bounds_.bottom) * 0.5F;
      for (const float offset : {-5.0F, 0.0F, 5.0F}) {
        render_target_->DrawLine(D2D1::Point2F(cx - 7.0F, cy + offset),
            D2D1::Point2F(cx + 7.0F, cy + offset), text_brush_.Get(), 1.0F);
      }
    }

    if (shadow_margin_ > 0.0F) {
      render_target_->PopAxisAlignedClip();
    }
    render_target_->SetTransform(D2D1::Matrix3x2F::Identity());
    const HRESULT draw_result = render_target_->EndDraw();
    if (draw_result == D2DERR_RECREATE_TARGET || FAILED(draw_result)) {
      DiscardDeviceResources();
      InvalidateRect(window_, nullptr, FALSE);
    } else if (layered_rendering_enabled_ && !PresentLayeredSurface()) {
      DiscardDeviceResources();
      if (!layered_present_retry_attempted_) {
        layered_present_retry_attempted_ = true;
        InvalidateRect(window_, nullptr, FALSE);
      }
    } else {
      layered_present_retry_attempted_ = false;
    }
  }

  EndPaint(window_, &paint);
}

void CandidateWindow::DiscardDeviceResources() {
  surface_bitmaps_ = {};
  preedit_format_.Reset();
  candidate_format_.Reset();
  annotation_format_.Reset();
  separator_brush_.Reset();
  accent_brush_.Reset();
  muted_brush_.Reset();
  highlighted_text_brush_.Reset();
  background_brush_.Reset();
  surface_border_brush_.Reset();
  preedit_brush_.Reset();
  text_brush_.Reset();
  render_target_.Reset();
  hwnd_render_target_.Reset();
  ReleaseLayeredSurface();
}

}  // namespace ziliu::ui
