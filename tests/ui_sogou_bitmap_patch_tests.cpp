#include "../src/ui/src/sogou_bitmap_patch.h"

#include <windows.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string_view>
#include <vector>

namespace {

using Microsoft::WRL::ComPtr;

void Expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

void DrawLegacyBitmapPatch(ID2D1RenderTarget* target, ID2D1Bitmap* bitmap,
                           const D2D1_RECT_F& source, const D2D1_RECT_F& destination) {
  constexpr float kTileWidth = 78.0F;
  for (float left = destination.left; left < destination.right; left += kTileWidth) {
    const float width = std::min(kTileWidth, destination.right - left);
    const D2D1_RECT_F source_tile =
        D2D1::RectF(source.left, source.top, source.left + width,
                    source.bottom);
    target->DrawBitmap(bitmap,
                       D2D1::RectF(left, destination.top, left + width, destination.bottom),
                       1.0F, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, source_tile);
  }
}

struct RenderResult {
  std::vector<std::uint32_t> pixels;
  int width = 0;
  int height = 0;
  int sample_y = 0;
  int content_left = 0;
  int content_right = 0;
  int content_bottom = 0;
  int color_min = 255;
  int color_max = 0;
  int alpha_min = 255;
  int alpha_max = 0;
};

RenderResult RenderPatch(float dpi, std::uint8_t source_alpha, bool legacy,
                         float translation_x = 0.0F, bool split_rows = false,
                         bool transparent_clear = false) {
  constexpr float kDestinationLeft = 109.0F;
  constexpr float kDestinationRight = 499.0F;
  constexpr float kDestinationHeight = 150.0F;
  const float scale = dpi / 96.0F;
  RenderResult result;
  result.width = static_cast<int>(std::ceil(520.0F * scale));
  result.height = static_cast<int>(std::ceil(160.0F * scale));

  HDC dc = CreateCompatibleDC(nullptr);
  Expect(dc != nullptr, "create offscreen bitmap DC");
  BITMAPINFO info{};
  info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biWidth = result.width;
  info.bmiHeader.biHeight = -result.height;
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;
  void* bits = nullptr;
  HBITMAP dib = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
  Expect(dib != nullptr && bits != nullptr, "create offscreen bitmap pixels");
  HGDIOBJ old_bitmap = SelectObject(dc, dib);
  Expect(old_bitmap != nullptr && old_bitmap != HGDI_ERROR, "select offscreen bitmap");

  ComPtr<ID2D1Factory> factory;
  Expect(SUCCEEDED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                                     factory.GetAddressOf())),
         "create Direct2D factory");
  const auto properties = D2D1::RenderTargetProperties(
      D2D1_RENDER_TARGET_TYPE_DEFAULT,
      D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                        D2D1_ALPHA_MODE_PREMULTIPLIED),
      dpi, dpi, D2D1_RENDER_TARGET_USAGE_NONE, D2D1_FEATURE_LEVEL_DEFAULT);
  ComPtr<ID2D1DCRenderTarget> target;
  Expect(SUCCEEDED(factory->CreateDCRenderTarget(&properties, target.GetAddressOf())),
         "create offscreen Direct2D target");
  const RECT bounds{0, 0, result.width, result.height};
  Expect(SUCCEEDED(target->BindDC(dc, &bounds)), "bind offscreen Direct2D target");
  target->SetTransform(D2D1::Matrix3x2F::Translation(translation_x, 0.0F));

  constexpr UINT kSourceWidth = 448;
  constexpr UINT kSourceHeight = 150;
  const std::uint32_t premultiplied =
      (static_cast<std::uint32_t>(source_alpha) << 24) |
      (static_cast<std::uint32_t>(source_alpha) << 16) |
      (static_cast<std::uint32_t>(source_alpha) << 8) |
      static_cast<std::uint32_t>(source_alpha);
  std::vector<std::uint32_t> source_pixels(kSourceWidth * kSourceHeight,
                                           premultiplied);
  ComPtr<ID2D1Bitmap> source_bitmap;
  const auto bitmap_properties = D2D1::BitmapProperties(
      D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                        D2D1_ALPHA_MODE_PREMULTIPLIED),
      96.0F, 96.0F);
  Expect(SUCCEEDED(target->CreateBitmap(D2D1::SizeU(kSourceWidth, kSourceHeight),
                                        source_pixels.data(), kSourceWidth * 4,
                                        bitmap_properties, source_bitmap.GetAddressOf())),
         "create synthetic Direct2D source bitmap");

  const D2D1_ANTIALIAS_MODE initial_antialias_mode = target->GetAntialiasMode();
  target->BeginDraw();
  target->Clear(transparent_clear
                    ? D2D1::ColorF(0.0F, 0.0F, 0.0F, 0.0F)
                    : D2D1::ColorF(64.0F / 255.0F, 64.0F / 255.0F,
                                   64.0F / 255.0F, 1.0F));
  const auto draw_patch = [&](float source_top, float source_bottom,
                              float destination_top, float destination_bottom) {
    const D2D1_RECT_F source =
        D2D1::RectF(109.0F, source_top, 187.0F, source_bottom);
    const D2D1_RECT_F destination = D2D1::RectF(
        kDestinationLeft, destination_top, kDestinationRight, destination_bottom);
    if (legacy) {
      DrawLegacyBitmapPatch(target.Get(), source_bitmap.Get(), source, destination);
    } else {
      ziliu::ui::detail::DrawSogouBitmapPatch(
          target.Get(), source_bitmap.Get(), source, destination,
          ziliu::core::ThemeImageLayout::kTile,
          ziliu::core::ThemeImageLayout::kStretch, 1.0F);
      Expect(target->GetAntialiasMode() == initial_antialias_mode,
             "bitmap patch rendering must restore the prior AA state");
    }
  };
  if (split_rows) {
    draw_patch(0.0F, 95.0F, 0.0F, 95.0F);
    draw_patch(95.0F, 150.0F, 95.0F, 150.0F);
  } else {
    draw_patch(0.0F, 150.0F, 0.0F, 150.0F);
  }
  Expect(SUCCEEDED(target->EndDraw()), "finish offscreen Direct2D draw");

  const auto transformed_pixel = [scale, translation_x](float value) {
    return static_cast<int>(std::lround((value + translation_x) * scale));
  };
  result.content_left = transformed_pixel(kDestinationLeft);
  result.content_right = transformed_pixel(kDestinationRight);
  result.content_bottom = static_cast<int>(std::lround(kDestinationHeight * scale));
  result.sample_y = static_cast<int>(std::lround(75.0F * scale));
  const auto* rendered = static_cast<const std::uint32_t*>(bits);
  result.pixels.assign(rendered,
                       rendered + static_cast<std::size_t>(result.width) * result.height);
  for (int y = 2; y < result.content_bottom - 2; ++y) {
    for (int x = result.content_left + 2; x < result.content_right - 2; ++x) {
      const std::uint32_t pixel = result.pixels[
          static_cast<std::size_t>(y) * result.width + x];
      const int color = static_cast<int>(pixel & 0xffU);
      const int alpha = static_cast<int>((pixel >> 24) & 0xffU);
      result.color_min = std::min(result.color_min, color);
      result.color_max = std::max(result.color_max, color);
      result.alpha_min = std::min(result.alpha_min, alpha);
      result.alpha_max = std::max(result.alpha_max, alpha);
    }
  }

  SelectObject(dc, old_bitmap);
  DeleteObject(dib);
  DeleteDC(dc);
  return result;
}

}  // namespace

int main() {
  const auto legacy_144_opaque = RenderPatch(144.0F, 255, true);
  Expect(legacy_144_opaque.color_min < legacy_144_opaque.color_max,
         "the legacy independent primitives must reproduce the 150% seam");

  const auto legacy_96_opaque = RenderPatch(96.0F, 255, true);
  const auto fixed_96_opaque = RenderPatch(96.0F, 255, false);
  Expect(legacy_96_opaque.pixels == fixed_96_opaque.pixels,
         "integer 96-DPI rendering must remain pixel-compatible");

  for (float dpi : {96.0F, 120.0F, 144.0F, 192.0F}) {
    const auto opaque = RenderPatch(dpi, 255, false);
    Expect(opaque.color_min == 255 && opaque.color_max == 255 &&
               opaque.alpha_min == 255 && opaque.alpha_max == 255,
           "opaque tiles must not leak the gray destination at internal seams");
    const auto translucent = RenderPatch(dpi, 128, false);
    Expect(translucent.color_max - translucent.color_min <= 1 &&
               translucent.alpha_min == 255 && translucent.alpha_max == 255,
           "translucent tiles must not overlap or darken at internal seams");
    std::cout << "BITMAP_PATCH dpi=" << dpi << " opaque=" << opaque.color_min << '-'
              << opaque.color_max << " alpha=" << opaque.alpha_min << '-'
              << opaque.alpha_max << " translucent=" << translucent.color_min << '-'
              << translucent.color_max << '\n';
  }

  const auto translated = RenderPatch(144.0F, 255, false, 0.25F);
  Expect(translated.color_min == 255 && translated.color_max == 255,
         "pixel snapping must include the active render-target translation");
  const auto split_opaque = RenderPatch(144.0F, 255, false, 0.0F, true);
  Expect(split_opaque.color_min == 255 && split_opaque.color_max == 255,
         "shared horizontal and vertical patch boundaries must not leak the destination");
  const auto split_translucent =
      RenderPatch(144.0F, 128, false, 0.0F, true, true);
  Expect(split_translucent.color_min == 128 && split_translucent.color_max == 128 &&
             split_translucent.alpha_min == 128 && split_translucent.alpha_max == 128,
         "transparent seams and intersections must contain one source layer exactly");
  std::cout << "BITMAP_PATCH split144 opaque=" << split_opaque.color_min << '-'
            << split_opaque.color_max << " translucent=" << split_translucent.color_min << '-'
            << split_translucent.color_max << " alpha=" << split_translucent.alpha_min << '-'
            << split_translucent.alpha_max << '\n';
  std::cout << "LEGACY_144 opaque=" << legacy_144_opaque.color_min << '-'
            << legacy_144_opaque.color_max << '\n';
  return EXIT_SUCCESS;
}
