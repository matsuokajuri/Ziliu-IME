#include "ziliu/ui/candidate_window.h"
#include "ziliu/core/sogou_theme.h"

#include <cstdlib>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <string_view>
#include <thread>
#include <fstream>
#include <iterator>

namespace ziliu::ui {
// Deterministic delivery of asynchronous geometry, without querying or taking
// focus on the user's desktop. Exercises the same queue/presentation methods.
struct CandidateWindowTestAccess {
  static void Queue(CandidateWindow& window, const core::CompositionSnapshot& snapshot,
                    const RECT& fallback, const core::Settings& settings) {
    window.QueuePresentation(snapshot, fallback, settings, 0);
  }
  static void Complete(CandidateWindow& window, const std::optional<RECT>& caret) {
    window.PresentAtCaret(caret);
  }
  static void SetTheme(CandidateWindow& window, const core::ThemeManifest& manifest,
                       const std::filesystem::path& directory) {
    window.DiscardDeviceResources();
    window.theme_manifest_ = manifest;
    window.theme_directory_ = directory;
    window.theme_initialized_ = true;
    window.settings_.active_theme_id = manifest.id;
  }
  static bool SetMemoryBackground(CandidateWindow& window, UINT height) {
    if (!window.EnsureDeviceResources()) return false;
    std::vector<std::uint32_t> pixels(static_cast<std::size_t>(height) * 2, 0xffffffffU);
    window.theme_manifest_.light.horizontal.background = core::ThemeImage{};
    const auto properties = D2D1::BitmapProperties(
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
    return SUCCEEDED(window.render_target_->CreateBitmap(D2D1::SizeU(2, height), pixels.data(), 8,
        properties, window.surface_bitmaps_.background.ReleaseAndGetAddressOf()));
  }
  static bool HasAuthoredInsets(const CandidateWindow& window) {
    return window.layout_scale_ == 1.0F && window.preedit_insets_.left == 11.0F &&
           window.preedit_insets_.top == 31.0F && window.candidate_insets_.left == 13.0F;
  }
  static bool CheckBackgroundRows(CandidateWindow& window, UINT top, UINT bottom) {
    constexpr UINT height = 220;
    if (!window.EnsureDeviceResources()) return false;
    // The host may be 144 DPI. Bind a 96-DPI pixel fixture explicitly instead
    // of comparing physical pixels against unscaled logical source rows.
    Microsoft::WRL::ComPtr<ID2D1DCRenderTarget> dc_target;
    if (FAILED(window.render_target_.As(&dc_target))) return false;
    struct RestoreTarget {
      ID2D1DCRenderTarget* target;
      HDC dc;
      RECT bounds;
      float dpi_x{}, dpi_y{};
      float& window_scale;
      float original_window_scale;
      ~RestoreTarget() {
        window_scale = original_window_scale;
        target->SetDpi(dpi_x, dpi_y);
        static_cast<void>(target->BindDC(dc, &bounds));
      }
    } restore{dc_target.Get(), window.layered_memory_dc_,
              RECT{0, 0, window.layered_pixel_size_.cx, window.layered_pixel_size_.cy},
              {}, {}, window.dpi_scale_, window.dpi_scale_};
    restore.target->GetDpi(&restore.dpi_x, &restore.dpi_y);
    const RECT fixture_bounds{0, 0, window.layered_pixel_size_.cx, static_cast<LONG>(height)};
    restore.target->SetDpi(96, 96);
    window.dpi_scale_ = 1.0F;
    if (FAILED(restore.target->BindDC(restore.dc, &fixture_bounds))) return false;
    std::vector<std::uint32_t> pixels(height * 4);
    for (UINT y = 0; y < height; ++y) {
      for (UINT x = 0; x < 4; ++x) {
        pixels[y * 4 + x] = 0xff000000U | (y << 16) | (x << 8) | 0x55U;
      }
    }
    auto& image = window.theme_manifest_.light.horizontal.background;
    image = core::ThemeImage{};
    image->stretch = core::ThemeInsets{1, top, 1, bottom};
    const auto properties = D2D1::BitmapProperties(
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
    window.surface_bitmaps_.background.Reset();
    if (FAILED(window.render_target_->CreateBitmap(D2D1::SizeU(4, height), pixels.data(), 16,
        properties, window.surface_bitmaps_.background.GetAddressOf()))) return false;
    window.render_target_->BeginDraw();
    window.DrawSurfaceBackground();
    if (FAILED(window.render_target_->EndDraw())) return false;
    // Inspect every original row in the fixed left/right edges, independently
    // of text. Compression, a missing middle, and palette fallback all fail.
    RECT client{};
    if (!GetClientRect(window.window_, &client) || client.right <= client.left) return false;
    const int right = client.right - client.left - 1;
    for (UINT y = 0; y < height; ++y) {
      if (GetPixel(window.layered_memory_dc_, 0, static_cast<int>(y)) != RGB(y, 0, 0x55) ||
          GetPixel(window.layered_memory_dc_, right, static_cast<int>(y)) != RGB(y, 3, 0x55)) {
        std::cerr << "BACKGROUND_ROW y=" << y << " left="
                  << GetPixel(window.layered_memory_dc_, 0, static_cast<int>(y))
                  << " right=" << GetPixel(window.layered_memory_dc_, right, static_cast<int>(y))
                  << " dpi=" << window.dpi_scale_ << " pixels=" << right + 1
                  << "x" << window.layered_pixel_size_.cy << '\n';
        return false;
      }
    }
    return true;
  }
  static float Height(const CandidateWindow& window) { return window.window_height_; }
  static float RowHeight(const CandidateWindow& window) { return window.candidate_row_height_; }
  static bool NativeFallback(const CandidateWindow& window) {
    return window.UsesNativeDefaultTheme() && !window.UsesSogouRendering() &&
        window.ActiveThemeAppearance() == core::MakeDefaultThemeManifest().light;
  }
  static float Scale(const CandidateWindow& window) { return window.layout_scale_; }
  static float SecondCandidateLeft(const CandidateWindow& window) {
    return window.candidate_lefts_.at(1);
  }
  static bool MenuFollowsLastCandidate(const CandidateWindow& window) {
    return !window.candidate_lefts_.empty() &&
        window.menu_button_bounds_.left >= window.candidate_lefts_.back() + window.candidate_widths_.back() &&
        window.menu_button_bounds_.right <= window.window_width_;
  }
  static LPARAM MenuClickPoint(const CandidateWindow& window) {
    const auto b = window.PresentedActionBounds(window.menu_button_bounds_);
    return MAKELPARAM(static_cast<WORD>((b.left + b.right) * 0.5F * window.dpi_scale_),
                      static_cast<WORD>((b.top + b.bottom) * 0.5F * window.dpi_scale_));
  }
  static bool SetMemoryAnimationBitmaps(CandidateWindow& window) {
    if (!window.EnsureDeviceResources()) return false;
    auto& appearance = window.theme_manifest_.light;
    auto& surface = window.settings_.candidate_layout == core::CandidateLayout::kHorizontal
        ? appearance.horizontal : appearance.vertical;
    auto& image = surface.background;
    image = core::ThemeImage{};
    image->stretch = core::ThemeInsets{1, 1, 1, 1};
    constexpr std::array<std::uint32_t, 9> background{
        0x80800000U, 0x80800000U, 0x80800000U,
        0x80800000U, 0x80800000U, 0x80800000U,
        0x80800000U, 0x80800000U, 0x80800000U};
    const auto properties = D2D1::BitmapProperties(
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
    window.surface_bitmaps_.background.Reset();
    if (FAILED(window.render_target_->CreateBitmap(
            D2D1::SizeU(3, 3), background.data(), 12, properties,
            window.surface_bitmaps_.background.GetAddressOf()))) {
      return false;
    }
    CandidateWindow::SurfaceBitmaps::OverlayBitmap overlay;
    overlay.overlay.align = {0, 0, 0, 0, 0, 2, 0, 2, 6, 0};
    constexpr std::array<std::uint32_t, 16> pixels{
        0xff00ff00U, 0xff00ff00U, 0xff00ff00U, 0xff00ff00U,
        0xff00ff00U, 0xff00ff00U, 0xff00ff00U, 0xff00ff00U,
        0xff00ff00U, 0xff00ff00U, 0xff00ff00U, 0xff00ff00U,
        0xff00ff00U, 0xff00ff00U, 0xff00ff00U, 0xff00ff00U};
    if (FAILED(window.render_target_->CreateBitmap(
            D2D1::SizeU(4, 4), pixels.data(), 16, properties,
            overlay.bitmap.GetAddressOf()))) {
      return false;
    }
    window.surface_bitmaps_.overlays.clear();
    window.surface_bitmaps_.overlays.push_back(std::move(overlay));
    return true;
  }
  static ID2D1Bitmap* BackgroundIdentity(const CandidateWindow& window) {
    return window.surface_bitmaps_.background.Get();
  }
  static std::string ThemeId(const CandidateWindow& window) {
    return window.theme_manifest_.id;
  }
  static float PresentedWidth(const CandidateWindow& window) {
    return window.PresentedContentWidth();
  }
  static float PresentedMenuRight(const CandidateWindow& window) {
    return window.PresentedActionBounds(window.menu_button_bounds_).right;
  }
  static float SurfaceOpacity(const CandidateWindow& window) {
    return window.surface_opacity_;
  }
  static std::uint32_t LayeredPixel(const CandidateWindow& window, LONG x, LONG y) {
    DIBSECTION section{};
    if (GetObjectW(window.layered_bitmap_, sizeof(section), &section) != sizeof(section) ||
        section.dsBm.bmBits == nullptr || x < 0 || y < 0 ||
        x >= section.dsBm.bmWidth || y >= std::abs(section.dsBm.bmHeight)) {
      return 0;
    }
    const auto* pixels = static_cast<const std::uint32_t*>(section.dsBm.bmBits);
    return pixels[static_cast<std::size_t>(y) * section.dsBm.bmWidth + x];
  }
  static bool CheckAnimatedSurface(const CandidateWindow& window) {
    RECT client{};
    if (!GetClientRect(window.window_, &client) || client.right < 6 || client.bottom < 6) {
      return false;
    }
    const std::uint32_t background = LayeredPixel(window, 1, 1);
    const std::uint32_t overlay = LayeredPixel(window, client.right - 1, client.bottom - 1);
    return (background >> 24U) == 0x80U && overlay == 0xff00ff00U;
  }
  static COLORREF SeparatorPixel(const CandidateWindow& window) {
    return GetPixel(window.layered_memory_dc_, static_cast<int>(100.0F * window.dpi_scale_),
                    static_cast<int>(window.preedit_height_ * window.dpi_scale_));
  }
  static bool SavePng(CandidateWindow& window, const std::filesystem::path& path) {
    using Microsoft::WRL::ComPtr;
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICBitmap> bitmap;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    if (!window.layered_bitmap_ ||
        FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&factory))) ||
        FAILED(factory->CreateBitmapFromHBITMAP(window.layered_bitmap_, nullptr,
                                                WICBitmapUsePremultipliedAlpha, &bitmap)) ||
        FAILED(factory->CreateStream(&stream)) ||
        FAILED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)) ||
        FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) ||
        FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) ||
        FAILED(encoder->CreateNewFrame(&frame, nullptr)) ||
        FAILED(frame->Initialize(nullptr)) ||
        FAILED(frame->WriteSource(bitmap.Get(), nullptr)) ||
        FAILED(frame->Commit()) || FAILED(encoder->Commit())) {
      return false;
    }
    return true;
  }
};
}  // namespace ziliu::ui

namespace {
struct AnimationFrameTiming {
  double time_ms;
  double work_ms;
};
thread_local WNDPROC original_window_procedure = nullptr;
thread_local bool measure_frames = false;
thread_local std::vector<AnimationFrameTiming> animation_frames;
thread_local bool track_positions = false;
thread_local std::vector<POINT> presented_positions;

double PreciseMilliseconds() {
  LARGE_INTEGER counter{}, frequency{};
  QueryPerformanceCounter(&counter);
  QueryPerformanceFrequency(&frequency);
  return static_cast<double>(counter.QuadPart) * 1000.0 / static_cast<double>(frequency.QuadPart);
}

LRESULT CALLBACK MeasureAnimationFrame(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  if (track_positions && message == WM_WINDOWPOSCHANGED) {
    RECT position{};
    GetWindowRect(window, &position);
    presented_positions.push_back({position.left, position.top});
  }
  const bool measure = measure_frames && message == WM_TIMER && wparam == 0x5A02;
  const double started = measure ? PreciseMilliseconds() : 0.0;
  const auto result = CallWindowProcW(original_window_procedure, window, message, wparam, lparam);
  if (measure) {
    animation_frames.push_back({started, PreciseMilliseconds() - started});
  }
  return result;
}

void ReportFrameTiming() {
  std::vector<double> intervals, work;
  for (std::size_t index = 0; index < animation_frames.size(); ++index) {
    work.push_back(animation_frames[index].work_ms);
    if (index != 0) {
      intervals.push_back(animation_frames[index].time_ms - animation_frames[index - 1].time_ms);
    }
  }
  std::sort(intervals.begin(), intervals.end());
  std::sort(work.begin(), work.end());
  if (!intervals.empty()) {
    std::cout << "ANIMATION_TIMING frames=" << animation_frames.size()
              << " interval_median_ms=" << intervals[intervals.size() / 2]
              << " interval_max_ms=" << intervals.back()
              << " work_median_ms=" << work[work.size() / 2]
              << " work_max_ms=" << work.back() << '\n';
  }
}

void Expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

HWND CreateHiddenOwner() {
  HWND owner = CreateWindowExW(0, L"STATIC", L"Ziliu hidden preview regression",
                               WS_OVERLAPPEDWINDOW, 80, 100, 800, 500, nullptr, nullptr,
                               GetModuleHandleW(nullptr), nullptr);
  Expect(owner != nullptr && !IsWindowVisible(owner), "test owner must remain hidden");
  return owner;
}

void PaintHiddenChild(HWND child, HWND owner) {
  Expect(GetParent(child) == owner && !IsWindowVisible(owner), "never paint a desktop popup");
  InvalidateRect(child, nullptr, FALSE);
  SendMessageW(child, WM_PAINT, 0, 0);
  Expect(!IsWindowVisible(child), "the preview must inherit the hidden owner's visibility");
}

void PumpFor(ULONGLONG milliseconds) {
  const auto started = GetTickCount64();
  do {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
    Sleep(1);
  } while (GetTickCount64() - started < milliseconds);
}

LONG WindowWidth(HWND window) {
  RECT bounds{};
  Expect(GetWindowRect(window, &bounds) != FALSE, "read popup rectangle");
  return bounds.right - bounds.left;
}

std::vector<std::byte> MakeTinyPng() {
  using Microsoft::WRL::ComPtr;
  ComPtr<IWICImagingFactory> factory;
  ComPtr<IWICBitmap> bitmap;
  ComPtr<IStream> stream;
  ComPtr<IWICBitmapEncoder> encoder;
  ComPtr<IWICBitmapFrameEncode> frame;
  BYTE pixel[4]{0, 0, 255, 255};
  Expect(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(&factory))) &&
             SUCCEEDED(factory->CreateBitmapFromMemory(1, 1, GUID_WICPixelFormat32bppBGRA,
                                                       4, sizeof(pixel), pixel, &bitmap)) &&
             SUCCEEDED(CreateStreamOnHGlobal(nullptr, TRUE, &stream)) &&
             SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) &&
             SUCCEEDED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) &&
             SUCCEEDED(encoder->CreateNewFrame(&frame, nullptr)) &&
             SUCCEEDED(frame->Initialize(nullptr)) &&
             SUCCEEDED(frame->WriteSource(bitmap.Get(), nullptr)) &&
             SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit()),
         "create an in-memory PNG for the remote theme test");
  STATSTG stat{};
  Expect(SUCCEEDED(stream->Stat(&stat, STATFLAG_NONAME)) && stat.cbSize.QuadPart > 0 &&
             stat.cbSize.QuadPart < 4096, "in-memory PNG has a bounded size");
  std::vector<std::byte> bytes(static_cast<std::size_t>(stat.cbSize.QuadPart));
  LARGE_INTEGER start{};
  ULONG read = 0;
  Expect(SUCCEEDED(stream->Seek(start, STREAM_SEEK_SET, nullptr)) &&
             SUCCEEDED(stream->Read(bytes.data(), static_cast<ULONG>(bytes.size()), &read)) &&
             read == bytes.size(), "read the in-memory PNG");
  return bytes;
}

void CheckRealWidthAnimation() {
  // Never show this popup on the input desktop. No SwitchDesktop, input injection,
  // registration, or VM is involved; the private desktop dies with this test.
  HDESK original = GetThreadDesktop(GetCurrentThreadId());
  const std::wstring name = L"ZiliuWidthTest-" + std::to_wstring(GetCurrentProcessId());
  HDESK isolated = CreateDesktopW(name.c_str(), nullptr, nullptr, 0, GENERIC_ALL, nullptr);
  Expect(isolated != nullptr && SetThreadDesktop(isolated), "attach test thread to private desktop");
  Expect(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)), "initialize isolated COM");
  HWND owner = CreateHiddenOwner();
  {
    ziliu::ui::CandidateWindow popup;
    Expect(popup.Create(owner), "create isolated candidate popup");
    HWND window = FindWindowW(L"Ziliu.CandidateWindow.v1", nullptr);
    Expect(window != nullptr, "find popup on private desktop");
    original_window_procedure = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
        window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(MeasureAnimationFrame)));
    Expect(original_window_procedure != nullptr, "observe real animation frame timings");
    ziliu::core::Settings settings;
    settings.theme_mode = ziliu::core::ThemeMode::kLight;
    settings.active_theme_id = ziliu::core::kDefaultThemeId;
    settings.candidate_count = 3;
    settings.candidate_layout = ziliu::core::CandidateLayout::kHorizontal;
    ziliu::core::CompositionSnapshot narrow;
    narrow.preedit = L"ni'hao";
    narrow.candidates = {{L"one", L"", 1.0}, {L"two", L"", 0.9}, {L"six", L"", 0.8}};
    auto wide = narrow;
    wide.candidates[0].text = L"a much longer first candidate";
    const RECT caret{100, 100, 102, 120};
    popup.Show(narrow, caret, settings, 0);
    PumpFor(200);
    const LONG narrow_width = WindowWidth(window);
    BOOL animate = FALSE;
    const bool enabled = SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animate, 0) && animate;
    animation_frames.clear();
    measure_frames = true;
    popup.Show(wide, caret, settings, 0);
    if (enabled) {
      Expect(WindowWidth(window) == narrow_width, "new content must not snap the popup width");
    }
    PumpFor(60);
    const LONG intermediate_width = WindowWidth(window);
    // An identical snapshot must preserve the running transition's deadline.
    popup.Show(wide, caret, settings, 0);
    PumpFor(150);
    measure_frames = false;
    ReportFrameTiming();
    const LONG wide_width = WindowWidth(window);
    Expect(wide_width > narrow_width, "fixture produces genuinely different widths");
    if (enabled) {
      Expect(narrow_width < intermediate_width && intermediate_width < wide_width,
             "real layered HWND must pass through an intermediate width after painting");
      popup.Show(narrow, caret, settings, 0);
      Expect(WindowWidth(window) == wide_width, "shrinking starts without a jump");
      PumpFor(60);
      const LONG shrinking_width = WindowWidth(window);
      Expect(narrow_width < shrinking_width && shrinking_width < wide_width, "shrinking is visible");
      popup.Show(wide, caret, settings, 0);
      Expect(WindowWidth(window) == shrinking_width, "rapid reversal continues at the displayed width");
      PumpFor(220);
      Expect(WindowWidth(window) == wide_width, "reversed animation reaches its exact target");
    } else {
      Expect(intermediate_width == wide_width, "Windows disabled animations must snap immediately");
    }
    popup.Show(narrow, caret, settings, 0);
    PumpFor(30);
    popup.Hide();
    PumpFor(150);
    Expect(!IsWindowVisible(window), "hide during resize must terminate and leave no visible popup");
    popup.Show(narrow, caret, settings, 0);
    PumpFor(200);
    SendMessageW(window, WM_ACTIVATEAPP, FALSE, 0);
    PumpFor(150);
    Expect(!IsWindowVisible(window), "deactivated app must not leave a candidate over another app");
    popup.Hide();
    PumpFor(150);
    using Access = ziliu::ui::CandidateWindowTestAccess;
    const RECT stale_top{100, 40, 102, 60};
    const RECT actual_bottom{100, 500, 102, 520};
    Access::Queue(popup, narrow, stale_top, settings);
    Expect(!IsWindowVisible(window), "first content update must wait for the position decision");
    Access::Complete(popup, actual_bottom);
    PumpFor(200);
    RECT bottom{};
    GetWindowRect(window, &bottom);
    presented_positions.clear();
    track_positions = true;
    for (int key = 0; key < 24; ++key) {
      Access::Queue(popup, key % 2 ? narrow : wide, stale_top, settings);
      // Timeout/null completion must retain the last verified caret, not TSF.
      Access::Complete(popup, key % 3 ? std::optional<RECT>(actual_bottom) : std::nullopt);
      // A Windows timer can be delivered about 30 ms apart here. Give each
      // alternating target enough time to produce a real HWND position event;
      // 17 ms can phase-align the reversals so no frame is ever presented.
      PumpFor(60);
      RECT current{};
      GetWindowRect(window, &current);
      Expect(current.top == bottom.top, "every input update keeps the verified vertical position");
    }
    PumpFor(220);
    track_positions = false;
    Expect(!presented_positions.empty(), "observe actual HWND moves, not just final screenshots");
    for (const POINT& position : presented_positions) {
      Expect(position.y == bottom.top, "no intermediate HWND move may flash at the stale top anchor");
    }
    popup.Hide();
    PumpFor(150);
    Access::Complete(popup, actual_bottom);
    Expect(!IsWindowVisible(window), "a late geometry reply after commit cannot resurrect candidates");
    Access::Queue(popup, narrow, stale_top, settings);
    Expect(!IsWindowVisible(window), "new composition does not inherit the previous caret");
    Access::Complete(popup, std::nullopt);
    PumpFor(200);
    RECT fallback{};
    GetWindowRect(window, &fallback);
    Expect(fallback.top != bottom.top, "unsupported providers still use native TSF fallback");
    Access::Complete(popup, actual_bottom);
    PumpFor(200);
    GetWindowRect(window, &fallback);
    Expect(fallback.top == bottom.top, "a newly available caret replaces fallback exactly once");
    std::cout << "Anchor arbitration PASS: 24 content updates, timeout, first-show, hide and fallback; "
              << presented_positions.size() << " HWND moves checked\n";
    popup.Hide();
    PumpFor(150);
    auto theme = ziliu::core::MakeDefaultThemeManifest();
    theme.id = "test.custom-ssf";
    theme.source_format = "sogou-ssf";
    theme.light.horizontal.background = ziliu::core::ThemeImage{};
    theme.light.typography.font_size = 20;
    theme.light.horizontal.preedit_insets = ziliu::core::ThemeInsets{11, 31, 17, 7};
    theme.light.horizontal.candidate_insets = ziliu::core::ThemeInsets{13, 4, 19, 3};
    Access::SetTheme(popup, theme, {});
    settings.active_theme_id = theme.id;
    settings.candidate_scale_with_text = true;
    settings.custom_candidate_font_size = false;
    popup.Show(narrow, caret, settings, 0);
    PumpFor(80);
    Expect(Access::HasAuthoredInsets(popup), "SSF declared font size must not magnify authored insets");
    theme.light.typography.sogou_use_gdip = 1U;
    theme.light.typography.chinese_font_family = "Arial";
    theme.light.typography.english_font_family = "Consolas";
    theme.light.horizontal.separator = ziliu::core::ThemeSeparator{0xffff0000U, "", 0, 0, 2};
    theme.light.horizontal.menu_button = ziliu::core::ThemeButtonImages{};
    Access::SetTheme(popup, theme, {});
    popup.Show(narrow, caret, settings, 0);
    PumpFor(80);
    auto missing_font_theme = theme;
    missing_font_theme.light.typography.chinese_font_family =
        "Ziliu Missing Font Fallback Test";
    Access::SetTheme(popup, missing_font_theme, {});
    auto missing_font_snapshot = narrow;
    missing_font_snapshot.candidates[0].text = L"你好";
    popup.Show(missing_font_snapshot, caret, settings, 0);
    PumpFor(80);
    Expect(Access::Height(popup) > 0,
           "SSF GDI rendering must retain a candidate surface for Chinese text with a missing authored font");
    Access::SetTheme(popup, theme, {});
    popup.Show(narrow, caret, settings, 0);
    PumpFor(80);
    const float short_second_left = Access::SecondCandidateLeft(popup);
    popup.Show(wide, caret, settings, 0);
    PumpFor(80);
    Expect(Access::SecondCandidateLeft(popup) > short_second_left,
           "SSF native measurement must advance the following candidate with content width");
    popup.Show(narrow, caret, settings, 0);
    PumpFor(80);
    const DWORD gdi_before = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    Expect(Access::MenuFollowsLastCandidate(popup), "SSF menu uses reserved space after the last candidate");
    int menu_calls = 0;
    popup.SetQuickMenuAction([&menu_calls](POINT) { ++menu_calls; });
    const LPARAM menu_point = Access::MenuClickPoint(popup);
    SendMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON, menu_point);
    SendMessageW(window, WM_LBUTTONUP, 0, menu_point);
    Expect(menu_calls == 1, "SSF menu click invokes the existing quick-menu callback once");
    popup.SetQuickMenuAction({});
    for (int repaint = 0; repaint < 32; ++repaint) {
      InvalidateRect(window, nullptr, FALSE);
      SendMessageW(window, WM_PAINT, 0, 0);
    }
    Expect(GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) == gdi_before,
           "repeated SSF mask rendering must release every temporary GDI object");
    PumpFor(160);
    const LONG custom_narrow_width = WindowWidth(window);
    popup.Show(wide, caret, settings, 0);
    Expect(Access::SetMemoryAnimationBitmaps(popup),
           "bind synthetic alpha background and right-edge overlay to the animation surface");
    ID2D1Bitmap* const animation_background = Access::BackgroundIdentity(popup);
    PumpFor(60);
    const LONG custom_intermediate_width = WindowWidth(window);
    if (enabled) {
      Expect(custom_narrow_width < custom_intermediate_width,
             "custom SSF width transition must expose an intermediate HWND width");
      Expect(Access::BackgroundIdentity(popup) == animation_background,
             "custom width frames must retain decoded theme bitmaps");
      Expect(std::fabs(Access::PresentedMenuRight(popup) - Access::PresentedWidth(popup)) < 1.1F,
             "custom right action must track the currently presented width");
    }
    InvalidateRect(window, nullptr, FALSE);
    SendMessageW(window, WM_PAINT, 0, 0);
    Expect(Access::CheckAnimatedSurface(popup),
           "custom intermediate frame preserves source alpha and right-edge overlay anchoring");
    PumpFor(180);
    const LONG custom_wide_width = WindowWidth(window);
    Expect(custom_wide_width > custom_narrow_width,
           "custom animation fixture produces genuinely different endpoint widths");
    popup.Show(narrow, caret, settings, 0);
    if (enabled) {
      PumpFor(60);
      const LONG custom_shrinking_width = WindowWidth(window);
      Expect(custom_narrow_width < custom_shrinking_width &&
                 custom_shrinking_width < custom_wide_width,
             "custom shrink exposes an intermediate width");
      popup.Show(wide, caret, settings, 0);
      Expect(WindowWidth(window) == custom_shrinking_width,
             "custom shrink reversal continues from the displayed width");
      PumpFor(220);
      Expect(WindowWidth(window) == custom_wide_width,
             "custom reversed width transition reaches its exact endpoint");
      popup.Show(narrow, caret, settings, 0);
    }
    PumpFor(220);
    InvalidateRect(window, nullptr, FALSE);
    SendMessageW(window, WM_PAINT, 0, 0);
    Expect(WindowWidth(window) == custom_narrow_width,
           "custom shrink reaches the exact narrow endpoint");
    Expect(Access::BackgroundIdentity(popup) == animation_background &&
               Access::CheckAnimatedSurface(popup),
           "custom shrink endpoint retains cached alpha artwork and current right-edge anchoring");
    popup.Hide();
    if (enabled) {
      PumpFor(45);
      Expect(IsWindowVisible(window) && Access::SurfaceOpacity(popup) > 0.0F &&
                 Access::SurfaceOpacity(popup) < 1.0F,
             "custom hide uses the default 80ms fade instead of snapping");
      const float hiding_opacity = Access::SurfaceOpacity(popup);
      popup.Show(narrow, caret, settings, 0);
      Expect(Access::SurfaceOpacity(popup) == hiding_opacity,
             "custom hide reversal starts at the currently presented opacity");
      PumpFor(60);
      Expect(Access::SurfaceOpacity(popup) > hiding_opacity &&
                 Access::SurfaceOpacity(popup) < 1.0F,
             "custom input reverses an in-flight hide fade without an opacity jump");
      PumpFor(150);
      Expect(Access::SurfaceOpacity(popup) == 1.0F,
             "custom reversed hide fade reaches the opaque endpoint");
      popup.Hide();
    }
    PumpFor(150);
    Expect(!IsWindowVisible(window), "custom fade-out reaches the hidden endpoint");
    popup.Show(narrow, caret, settings, 0);
    if (enabled) {
      PumpFor(50);
      Expect(Access::SurfaceOpacity(popup) > 0.0F && Access::SurfaceOpacity(popup) < 1.0F,
             "custom first show uses the default 110ms fade");
    }
    PumpFor(160);
    Expect(Access::SurfaceOpacity(popup) == 1.0F,
           "custom fade-in reaches the exact opaque endpoint");
    auto unicode_preedit = narrow;
    unicode_preedit.preedit = L"你好";
    popup.Show(unicode_preedit, caret, settings, 0);
    PumpFor(80);
    Expect(Access::Height(popup) > 0, "non-ASCII preedit retains the fallback renderer");
    popup.Show(narrow, caret, settings, 0);
    PumpFor(80);
    Expect(Access::SetMemoryBackground(popup, 220), "create synthetic H1 background");
    popup.Show(narrow, caret, settings, 0);
    Expect(Access::Height(popup) == 220, "short H1 text must preserve natural background height");
    // Resizing recreates the D2D target. This fixture has no on-disk asset,
    // so rebind its memory bitmap after the layout resize before pixel checks.
    Expect(Access::SetMemoryBackground(popup, 220), "rebind resized synthetic H1 background");
    InvalidateRect(window, nullptr, FALSE);
    SendMessageW(window, WM_PAINT, 0, 0);
    std::cout << "SSF_SEPARATOR_PIXEL actual=" << Access::SeparatorPixel(popup)
              << " expected=" << RGB(255, 255, 255) << '\n';
    Expect(Access::SeparatorPixel(popup) == RGB(255, 255, 255),
           "native H1 must not inject a legacy color-only separator over its background");
    Expect(Access::CheckBackgroundRows(popup, 80, 40),
           "normal H1 slices retain all source rows at natural height");
    Expect(Access::CheckBackgroundRows(popup, 140, 80),
           "zero-center H1 retains all rows instead of discarding its background");
    Expect(Access::CheckBackgroundRows(popup, 160, 100),
           "overlapping H1 cuts retain original rows without compression or duplication");
    settings.custom_candidate_font_size = true;
    settings.candidate_font_size = 24;
    popup.Show(narrow, caret, settings, 0);
    PumpFor(80);
    Expect(Access::Scale(popup) == 1,
           "stored user font-size overrides must not rescale an active custom skin");
    Expect(Access::SetMemoryBackground(popup, 4096), "create oversized synthetic H1 background");
    popup.Show(narrow, caret, settings, 0);
    Expect(Access::Height(popup) > 0 && Access::Height(popup) < 4096,
           "natural image height must not bypass work-area limits");
    settings.candidate_layout = ziliu::core::CandidateLayout::kVertical;
    settings.custom_candidate_font_size = false;
    popup.Show(narrow, caret, settings, 0);
    Expect(Access::NativeFallback(popup),
           "unsupported custom V1 uses the complete native appearance, not SSF fonts or colors");
    settings.candidate_layout = ziliu::core::CandidateLayout::kHorizontal;
    popup.Show(narrow, caret, settings, 0);
    Expect(!Access::NativeFallback(popup), "return to H1 retains the selected SSF theme");
    auto vertical_theme = theme;
    vertical_theme.light.vertical = theme.light.horizontal;
    for (const std::uint32_t size : {20U, 24U}) {
      vertical_theme.light.typography.font_size = size;
      Access::SetTheme(popup, vertical_theme, {});
      settings.candidate_layout = ziliu::core::CandidateLayout::kVertical;
      popup.Show(narrow, caret, settings, 0);
      Expect(!Access::NativeFallback(popup) &&
                 Access::RowHeight(popup) == static_cast<float>(size) + 4.0F,
             "supported V1 uses authored text rows instead of the native minimum");
    }
    settings.custom_theme_scale_with_windows = false;
    popup.Show(narrow, caret, settings, 0);
    PumpFor(220);
    const LONG custom_v1_narrow_width = WindowWidth(window);
    auto v1_wide = wide;
    v1_wide.candidates[0].text =
        L"a much longer vertical candidate that exceeds the authored V1 minimum width";
    popup.Show(v1_wide, caret, settings, 0);
    Expect(Access::SetMemoryAnimationBitmaps(popup),
           "bind synthetic V1 background with Windows theme scaling disabled");
    ID2D1Bitmap* const v1_background = Access::BackgroundIdentity(popup);
    PumpFor(60);
    if (enabled) {
      Expect(custom_v1_narrow_width < WindowWidth(window),
             "real custom V1 exposes an intermediate width");
      Expect(Access::BackgroundIdentity(popup) == v1_background,
             "custom V1 frames retain decoded theme bitmaps");
    }
    InvalidateRect(window, nullptr, FALSE);
    SendMessageW(window, WM_PAINT, 0, 0);
    Expect(Access::CheckAnimatedSurface(popup),
           "custom V1 intermediate frame anchors current-width alpha artwork");
    PumpFor(180);
    Expect(WindowWidth(window) > custom_v1_narrow_width,
           "custom V1 reaches its wider endpoint with theme scaling disabled");
    settings.candidate_layout = ziliu::core::CandidateLayout::kHorizontal;
    settings.custom_theme_scale_with_windows = true;
    popup.Hide();
    PumpFor(150);
    auto remote_theme = ziliu::core::MakeDefaultThemeManifest();
    remote_theme.id = "ziliu.remote-test";
    remote_theme.source_format = "sogou-ssf";
    remote_theme.light.horizontal.background = ziliu::core::ThemeImage{};
    remote_theme.light.horizontal.background->asset = "assets/remote.png";
    const std::string remote_manifest = ziliu::core::SerializeThemeManifest(remote_theme);
    const auto remote_png = MakeTinyPng();
    int manifest_reads = 0;
    int asset_reads = 0;
    popup.SetThemeResourceLoader([&](std::string_view id, std::string_view resource)
                                     -> std::optional<std::vector<std::byte>> {
      if (id != remote_theme.id) return std::nullopt;
      if (resource.empty()) {
        ++manifest_reads;
        const auto* first = reinterpret_cast<const std::byte*>(remote_manifest.data());
        return std::vector<std::byte>(first, first + remote_manifest.size());
      }
      if (resource == "assets/remote.png") {
        ++asset_reads;
        return remote_png;
      }
      return std::nullopt;
    });
    settings.active_theme_id = remote_theme.id;
    popup.Show(narrow, caret, settings, 0);
    PumpFor(80);
    Expect(Access::ThemeId(popup) == remote_theme.id && manifest_reads == 1 &&
               asset_reads > 0 && Access::BackgroundIdentity(popup) != nullptr,
           "restricted-host theme manifest and PNG load without filesystem access");
    popup.Hide();
    PumpFor(150);
    const auto default_theme = ziliu::core::MakeDefaultThemeManifest();
    Access::SetTheme(popup, default_theme, {});
    settings.active_theme_id = default_theme.id;
    settings.custom_candidate_font_size = false;
    popup.Show(narrow, caret, settings, 0);
    PumpFor(220);
    Expect(WindowWidth(window) == narrow_width, "returning from SSF preserves the native baseline layout");
    popup.Hide();
    std::cout << "Authored SSF insets, H1 height limits, override and native-return checks PASS\n";
    std::cout << "Isolated layered HWND width transition checks PASS; animation enabled=" << enabled << '\n';
  }
  DestroyWindow(owner);
  CoUninitialize();
  Expect(SetThreadDesktop(original) != FALSE, "detach private test desktop");
  Expect(CloseDesktop(isolated) != FALSE, "release private test desktop");
}
}  // namespace

// This diagnostic mode consumes an extracted UTF-8 skin.ini and original assets.
// It deliberately selects H1 only; it does not certify SSF import or TSF input.
int CaptureSsfH1(const std::filesystem::path& source, const std::filesystem::path& output) {
  std::ifstream input(source / "skin.ini", std::ios::binary);
  Expect(static_cast<bool>(input), "read extracted UTF-8 skin.ini");
  const std::string ini{std::istreambuf_iterator<char>(input), {}};
  std::string selected;
  bool include = false;
  for (std::size_t pos = 0; pos < ini.size();) {
    const auto end = ini.find('\n', pos);
    const auto line = ini.substr(pos, end == std::string::npos ? end : end - pos);
    if (!line.empty() && line.front() == '[') {
      include = line.starts_with("[General]") || line.starts_with("[Display]") ||
                line.starts_with("[Scheme_H1]");
    }
    if (include) selected += line + '\n';
    if (end == std::string::npos) break;
    pos = end + 1;
  }
  const auto conversion = ziliu::core::ConvertSogouThemeIni(selected, "geometry.ssf");
  for (const auto& issue : conversion.issues) std::cerr << issue.path << ": " << issue.message << '\n';
  Expect(conversion.ok(), "convert selected custom H1 without guessed fields");
  const auto resources = output.parent_path() / (output.stem().string() + "-resources");
  Expect(!std::filesystem::exists(resources) && !std::filesystem::exists(output), "fresh output only");
  std::filesystem::create_directories(resources);
  for (const auto& asset : conversion.assets) {
    const auto destination = resources / asset.target_path;
    std::filesystem::create_directories(destination.parent_path());
    Expect(std::filesystem::path(asset.source_path).extension() == ".png", "diagnostic copies PNG assets only");
    std::filesystem::copy_file(source / asset.source_path, destination);
  }
  std::ofstream(resources / "manifest.json") << ziliu::core::SerializeThemeManifest(conversion.manifest);
  std::thread render([&] {
    HDESK original = GetThreadDesktop(GetCurrentThreadId());
    const std::wstring name = L"ZiliuSsfGeometry-" + std::to_wstring(GetCurrentProcessId());
    HDESK isolated = CreateDesktopW(name.c_str(), nullptr, nullptr, 0, GENERIC_ALL, nullptr);
    Expect(isolated && SetThreadDesktop(isolated), "private render desktop");
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_UNAWARE);
    Expect(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)), "render COM");
    HWND owner = CreateHiddenOwner();
    {
      ziliu::ui::CandidateWindow popup;
      Expect(popup.Create(owner), "create private candidate surface");
      ziliu::ui::CandidateWindowTestAccess::SetTheme(popup, conversion.manifest, resources);
      ziliu::core::Settings settings;
      settings.theme_mode = ziliu::core::ThemeMode::kLight;
      settings.active_theme_id = conversion.manifest.id;
      settings.candidate_count = 7;
      settings.candidate_layout = ziliu::core::CandidateLayout::kHorizontal;
      ziliu::core::CompositionSnapshot snapshot;
      snapshot.preedit = L"ni'hao";
      snapshot.candidates = {{L"你好", L"", 1}, {L"你是", L"", 1}, {L"不好", L"", 1},
                             {L"拟好", L"", 1}, {L"你还", L"", 1}, {L"拨号", L"", 1}, {L"你", L"", 1}};
      popup.Show(snapshot, RECT{100, 100, 101, 120}, settings, 0);
      PumpFor(250);
      HWND window = FindWindowW(L"Ziliu.CandidateWindow.v1", nullptr);
      Expect(window && GetDpiForWindow(window) == 96, "exact 96-DPI surface");
      Expect(ziliu::ui::CandidateWindowTestAccess::SavePng(popup, output), "write native rendered pixels");
      RECT rect{}; GetWindowRect(window, &rect);
      std::cout << "H1_RENDER width=" << rect.right - rect.left << " height=" << rect.bottom - rect.top
                << " dpi=96 candidates=7 source=private-desktop-render NOT_REAL_INPUT_CAPTURE\n";
    }
    DestroyWindow(owner);
    CoUninitialize();
    Expect(SetThreadDesktop(original) && CloseDesktop(isolated), "release private desktop");
  });
  render.join();
  return EXIT_SUCCESS;
}

int main(int argc, char* argv[]) {
  using Access = ziliu::ui::CandidateWindowTestAccess;
  if (argc == 4 && std::string_view(argv[1]) == "--capture-ssf-h1") {
    return CaptureSsfH1(argv[2], argv[3]);
  }
  if (argc != 1) {
    std::cerr << "Usage: ziliu_ui_preview_window_tests [--capture-ssf-h1 source-dir output.png]\n";
    return EXIT_FAILURE;
  }
  Expect(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)), "initialize COM");
  HWND owner = CreateHiddenOwner();
  {
    ziliu::ui::CandidateWindow preview;
    Expect(!preview.CreatePreview(nullptr), "a preview requires a valid owner");
    Expect(preview.CreatePreview(owner), "create the native preview");
    HWND child = FindWindowExW(owner, nullptr, L"Ziliu.CandidatePreview.v1", nullptr);
    Expect(child != nullptr && GetParent(child) == owner, "preview must be a child of Settings");
    const LONG_PTR style = GetWindowLongPtrW(child, GWL_STYLE);
    Expect((style & WS_CHILD) != 0 && (style & WS_POPUP) == 0,
           "reject the old detached popup path before any show operation");

    ziliu::core::Settings settings;
    settings.theme_mode = ziliu::core::ThemeMode::kLight;
    settings.active_theme_id = ziliu::core::kDefaultThemeId;
    settings.candidate_count = 3;
    settings.candidate_layout = ziliu::core::CandidateLayout::kHorizontal;
    ziliu::core::CompositionSnapshot snapshot;
    snapshot.preedit = L"ni'hao";
    snapshot.candidates = {{L"你好", L"", 1.0}, {L"世界", L"", 0.9}, {L"示例", L"", 0.8}};
    const RECT host_bounds{20, 30, 720, 250};
    preview.ShowPreview(snapshot, host_bounds, settings, 0);
    RECT before_paint{};
    GetWindowRect(child, &before_paint);
    PaintHiddenChild(child, owner);
    RECT after_paint{};
    GetWindowRect(child, &after_paint);
    Expect(EqualRect(&before_paint, &after_paint),
           "layered presentation must not reinterpret owner-client coordinates as screen coordinates");
    RECT relative = after_paint;
    MapWindowPoints(nullptr, owner, reinterpret_cast<POINT*>(&relative), 2);
    Expect(relative.left >= host_bounds.left && relative.top >= host_bounds.top &&
               relative.right <= host_bounds.right && relative.bottom <= host_bounds.bottom,
           "preview stays within its XAML host bounds");

    RECT owner_before{};
    GetWindowRect(owner, &owner_before);
    SetWindowPos(owner, nullptr, owner_before.left + 140, owner_before.top + 70, 0, 0,
                  SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER);
    PaintHiddenChild(child, owner);
    RECT moved{};
    GetWindowRect(child, &moved);
    Expect(moved.left - after_paint.left == 140 && moved.top - after_paint.top == 70,
           "preview must move with Settings without a layout refresh");
    Expect(SendMessageW(child, WM_NCHITTEST, 0, 0) == HTTRANSPARENT,
           "preview must leave pointer input to the XAML page");

    auto preview_theme = ziliu::core::MakeDefaultThemeManifest();
    preview_theme.id = "test.preview-custom-ssf";
    preview_theme.source_format = "sogou-ssf";
    preview_theme.light.horizontal.background = ziliu::core::ThemeImage{};
    Access::SetTheme(preview, preview_theme, {});
    settings.active_theme_id = preview_theme.id;
    auto preview_wide = snapshot;
    preview_wide.candidates[0].text = L"a much longer preview candidate";
    preview.ShowPreview(preview_wide, host_bounds, settings, 0);
    PaintHiddenChild(child, owner);
    const LONG preview_wide_width = WindowWidth(child);
    Expect(Access::SetMemoryAnimationBitmaps(preview),
           "bind custom preview artwork before non-animated shrink");
    ID2D1Bitmap* const preview_background = Access::BackgroundIdentity(preview);
    preview.ShowPreview(snapshot, host_bounds, settings, 0);
    PaintHiddenChild(child, owner);
    Expect(WindowWidth(child) < preview_wide_width,
           "custom preview shrink snaps to the narrower endpoint without animation");
    Expect(Access::BackgroundIdentity(preview) == preview_background &&
               Access::CheckAnimatedSurface(preview),
           "non-animated custom preview shrink uses client width and retains cached right-edge artwork");
    GetWindowRect(child, &relative);
    MapWindowPoints(nullptr, owner, reinterpret_cast<POINT*>(&relative), 2);

    const RECT viewport{relative.left + 13, relative.top + 7,
                        relative.right - 11, relative.bottom - 5};
    preview.ShowPreview(snapshot, host_bounds, settings, 0, &viewport);
    HRGN region = CreateRectRgn(0, 0, 0, 0);
    Expect(GetWindowRgn(child, region) != ERROR, "preview must expose a viewport clipping region");
    RECT clipped{};
    GetRgnBox(region, &clipped);
    DeleteObject(region);
    const RECT expected{13, 7, relative.right - relative.left - 11,
                        relative.bottom - relative.top - 5};
    Expect(EqualRect(&clipped, &expected), "scroll clipping must use child-local pixels");
    const RECT outside{0, 0, 1, 1};
    preview.ShowPreview(snapshot, host_bounds, settings, 0, &outside);
    Expect((GetWindowLongPtrW(child, GWL_STYLE) & WS_VISIBLE) == 0,
           "a preview outside the viewport must be hidden");
    preview.ShowPreview(snapshot, host_bounds, settings, 0);
    preview.Hide();
    Expect((GetWindowLongPtrW(child, GWL_STYLE) & WS_VISIBLE) == 0,
           "leaving Appearance must hide the preview");
    DestroyWindow(owner);
    Expect(!IsWindow(child), "closing Settings must destroy its preview");
    owner = CreateHiddenOwner();
    Expect(preview.CreatePreview(owner), "preview can be recreated after owner destruction");
  }
  Expect(FindWindowExW(owner, nullptr, L"Ziliu.CandidatePreview.v1", nullptr) == nullptr,
         "preview destruction leaves no orphan window");
  DestroyWindow(owner);
  CoUninitialize();
  std::thread animation_test(CheckRealWidthAnimation);
  animation_test.join();
  std::cout << "Hidden preview parent, move, clipping, input and destruction checks PASS\n";
  return EXIT_SUCCESS;
}
