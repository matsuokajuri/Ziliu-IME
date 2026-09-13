#include "../src/ui/src/native_candidate_geometry.h"

#include <wincodec.h>

#include <cstdlib>
#include <iostream>
#include <string_view>
#include <vector>

namespace {
void Expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

std::vector<BYTE> RenderText(IWICImagingFactory* wic, ID2D1Factory* factory,
                             const ziliu::ui::detail::NativeTextLine& line,
                             float scale, bool clip) {
  const UINT width = static_cast<UINT>(std::ceil((line.width + 16.0F) * scale));
  const UINT height = static_cast<UINT>(std::ceil((line.height + 16.0F) * scale));
  Microsoft::WRL::ComPtr<IWICBitmap> bitmap;
  Expect(SUCCEEDED(wic->CreateBitmap(width, height, GUID_WICPixelFormat32bppPBGRA,
                                     WICBitmapCacheOnLoad, bitmap.GetAddressOf())), "create offscreen bitmap");
  Microsoft::WRL::ComPtr<ID2D1RenderTarget> target;
  const auto properties = D2D1::RenderTargetProperties(
      D2D1_RENDER_TARGET_TYPE_SOFTWARE,
      D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
      96.0F * scale, 96.0F * scale);
  Expect(SUCCEEDED(factory->CreateWicBitmapRenderTarget(bitmap.Get(), properties,
                                                       target.GetAddressOf())), "create windowless render target");
  Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
  Expect(SUCCEEDED(target->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Black),
                                                brush.GetAddressOf())), "create text brush");
  target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
  target->BeginDraw();
  target->Clear(D2D1::ColorF(D2D1::ColorF::White));
  if (clip) {
    target->PushAxisAlignedClip(D2D1::RectF(8.0F, 8.0F, 8.0F + line.width, 8.0F + line.height),
                                D2D1_ANTIALIAS_MODE_ALIASED);
  }
  target->DrawTextLayout(D2D1::Point2F(8.0F + line.origin_offset.x, 8.0F + line.origin_offset.y),
                         line.layout.Get(), brush.Get());
  if (clip) {
    target->PopAxisAlignedClip();
  }
  Expect(SUCCEEDED(target->EndDraw()), "render offscreen text");
  std::vector<BYTE> pixels(static_cast<std::size_t>(width) * height * 4);
  Expect(SUCCEEDED(bitmap->CopyPixels(nullptr, width * 4, static_cast<UINT>(pixels.size()),
                                      pixels.data())), "read offscreen text pixels");
  return pixels;
}

void CheckShadow(IWICImagingFactory* wic, ID2D1Factory* factory) {
  Microsoft::WRL::ComPtr<IWICBitmap> bitmap;
  Expect(SUCCEEDED(wic->CreateBitmap(124, 84, GUID_WICPixelFormat32bppPBGRA,
                                     WICBitmapCacheOnLoad, bitmap.GetAddressOf())), "create shadow bitmap");
  Microsoft::WRL::ComPtr<ID2D1RenderTarget> target;
  const auto properties = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
      D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
  Expect(SUCCEEDED(factory->CreateWicBitmapRenderTarget(bitmap.Get(), properties,
                                                       target.GetAddressOf())), "create shadow target");
  target->BeginDraw();
  target->Clear(D2D1::ColorF(0, 0, 0, 0));
  ziliu::ui::detail::DrawNativeCandidateShadow(factory, target.Get(),
      D2D1::RectF(12, 12, 112, 72), 12, 1.0F, false);
  Expect(SUCCEEDED(target->EndDraw()), "draw shadow");
  std::vector<BYTE> pixels(124 * 84 * 4);
  Expect(SUCCEEDED(bitmap->CopyPixels(nullptr, 124 * 4, static_cast<UINT>(pixels.size()), pixels.data())),
         "read shadow pixels");
  const auto alpha = [&pixels](std::size_t x, std::size_t y) { return pixels[(y * 124 + x) * 4 + 3]; };
  Expect(alpha(10, 42) > 0 && alpha(10, 42) < 80, "a soft translucent shadow extends outside the frame");
  Expect(alpha(62, 75) > alpha(62, 9), "shadow is gently offset below the frame");
  for (std::size_t x = 0; x < 124; ++x) {
    Expect(alpha(x, 0) == 0 && alpha(x, 83) == 0, "shadow fades before the top and bottom canvas edges");
  }
  for (std::size_t y = 0; y < 84; ++y) {
    Expect(alpha(0, y) == 0 && alpha(123, y) == 0, "shadow fades before the side canvas edges");
  }
}
}  // namespace

int main() {
  using namespace ziliu::ui::detail;
  Expect(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)), "initialize COM");
  {
    Microsoft::WRL::ComPtr<IDWriteFactory> text_factory;
    Microsoft::WRL::ComPtr<ID2D1Factory> factory;
    Microsoft::WRL::ComPtr<IWICImagingFactory> wic;
    Expect(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(text_factory.GetAddressOf()))), "create DirectWrite factory");
    Expect(SUCCEEDED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory.GetAddressOf())),
           "create Direct2D factory");
    Expect(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_PPV_ARGS(wic.GetAddressOf()))), "create WIC factory");
    CheckShadow(wic.Get(), factory.Get());
    Expect(NativeFadeOpacity(0, 1, 0, 110) == 0 && NativeFadeOpacity(0, 1, 110, 110) == 1,
           "appearance fade has exact endpoints");
    Expect(NativeFadeOpacity(1, 0, 80, 80) == 0 && NativeFadeOpacity(0, 1, 0, 0) == 1,
           "hide and disabled-duration fades terminate");
    float previous = 0.0F;
    for (int elapsed = 0; elapsed <= 130; ++elapsed) {
      const float value = NativeFadeOpacity(0, 1, static_cast<float>(elapsed), 110);
      Expect(value >= previous && value <= 1.0F, "appearance fade is monotonic without overshoot");
      previous = value;
    }
    const float interrupted = NativeFadeOpacity(1, 0, 30, 80);
    Expect(NativeFadeOpacity(interrupted, 1, 0, 80) == interrupted,
           "new input can reverse a hide fade without an opacity jump");
    std::size_t cases = 0;
    for (const wchar_t* family : {L"Segoe UI Variable Text", L"Arial", L"Source Han Sans SC"}) {
      for (const float size : {12.0F, 18.0F, 24.0F, 48.0F}) {
        Microsoft::WRL::ComPtr<IDWriteTextFormat> format;
        Expect(SUCCEEDED(text_factory->CreateTextFormat(family, nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"zh-CN", format.GetAddressOf())),
            "create test font");
        for (const float scale : {1.0F, 1.25F, 1.5F, 2.0F}) {
          for (const std::wstring_view text : {L"ni'hao", L"qiong'ping", L"Üǚgj 你好"}) {
            const auto line = MeasureNativeTextLine(text_factory.Get(), format.Get(), text, 2048.0F, scale);
            Expect(line.layout != nullptr && line.width > 0.0F && line.height > 0.0F, "measure real line bounds");
            Expect(RenderText(wic.Get(), factory.Get(), line, scale, true) ==
                       RenderText(wic.Get(), factory.Get(), line, scale, false),
                   "the measured clip must retain every rendered glyph pixel");
            Expect(NativeCandidateRowHeight(line.height, 1.0F) >= line.height + 4.0F,
                   "compact rows retain text clearance");
            ++cases;
          }
        }
      }
    }
    const auto curve = CreateNativeRoundedGeometry(factory.Get(), D2D1::RectF(0, 0, 100, 60), 12);
    Expect(curve != nullptr, "create cubic rounded surface");
    D2D1_RECT_F bounds{};
    Expect(SUCCEEDED(curve->GetBounds(nullptr, &bounds)) && bounds.left == 0.0F && bounds.top == 0.0F &&
               bounds.right == 100.0F && bounds.bottom == 60.0F, "cubic path preserves surface extents");
    BOOL inside = FALSE;
    Expect(SUCCEEDED(curve->FillContainsPoint(D2D1::Point2F(0, 0), nullptr, &inside)) && !inside,
           "the outside corner remains transparent");
    Expect(SUCCEEDED(curve->FillContainsPoint(D2D1::Point2F(3, 3), nullptr, &inside)) && inside,
           "continuous cubic corners differ from circular arcs");
    Expect(CreateNativeRoundedGeometry(factory.Get(), D2D1::RectF(0, 0, 0, 1), 12) == nullptr,
           "reject empty surfaces");
    Expect(CreateNativeRoundedGeometry(factory.Get(), D2D1::RectF(0, 0, 4, 4), 12) != nullptr,
           "clamp corner reach on small surfaces");
    std::cout << "Windowless native text clipping cases: " << cases << " PASS\n";
  }
  CoUninitialize();
  return EXIT_SUCCESS;
}
