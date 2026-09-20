#include "../src/ui/src/sogou_gdi_raster_scale.h"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace {

void Expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

void ExpectNear(float actual, float expected, std::string_view message) {
  Expect(std::abs(actual - expected) < 0.001F, message);
}

struct GdiMaskSample {
  int font_pixels = 0;
  int cell_height = 0;
  int text_width = 0;
  int prefix_width = 0;
  int prefix_slot = 0;
  int mask_width = 0;
  int rightmost_ink = -1;
  std::size_t ink_pixels = 0;
};

GdiMaskSample SampleMask(float dpi, const wchar_t* family, std::wstring_view text,
                         bool prefix) {
  const auto raster = ziliu::ui::detail::ResolveSogouGdiRasterScale(dpi);
  GdiMaskSample sample;
  sample.font_pixels = raster.ToPhysicalPixels(24.0F);
  HDC dc = CreateCompatibleDC(nullptr);
  Expect(dc != nullptr, "create memory DC for the SSF mask fixture");
  HFONT font = CreateFontW(-sample.font_pixels, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           CLEARTYPE_QUALITY, DEFAULT_PITCH, family);
  Expect(font != nullptr, "create the DPI-sized GDI font");
  HGDIOBJ old_font = SelectObject(dc, font);
  Expect(old_font != nullptr && old_font != HGDI_ERROR, "select the DPI-sized GDI font");

  TEXTMETRICW metrics{};
  SIZE text_extent{};
  Expect(GetTextMetricsW(dc, &metrics) != FALSE &&
             GetTextExtentPoint32W(dc, text.data(), static_cast<int>(text.size()),
                                   &text_extent) != FALSE,
         "measure the actual GDI mask text");
  sample.cell_height = metrics.tmHeight;
  sample.text_width = text_extent.cx;
  if (prefix) {
    SIZE prefix_extent{};
    Expect(GetTextExtentPoint32W(dc, L"1.", 2, &prefix_extent) != FALSE,
           "measure the actual GDI prefix");
    sample.prefix_width = prefix_extent.cx;
    sample.prefix_slot = sample.prefix_width + raster.ToPhysicalPixels(2.0F);
  }
  sample.mask_width = sample.prefix_slot + sample.text_width;
  Expect(sample.mask_width > sample.prefix_slot && sample.cell_height > 0,
         "measured text and prefix must fit a non-empty mask");

  BITMAPINFO info{};
  info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biWidth = sample.mask_width;
  info.bmiHeader.biHeight = -sample.cell_height;
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;
  void* bits = nullptr;
  HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
  Expect(bitmap != nullptr && bits != nullptr, "allocate the actual GDI mask DIB");
  HGDIOBJ old_bitmap = SelectObject(dc, bitmap);
  Expect(old_bitmap != nullptr && old_bitmap != HGDI_ERROR, "select the actual GDI mask DIB");
  const auto count = static_cast<std::size_t>(sample.mask_width) *
                     static_cast<std::size_t>(sample.cell_height);
  auto* pixels = static_cast<std::uint32_t*>(bits);
  std::fill_n(pixels, count, 0U);
  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, RGB(255, 255, 255));
  if (prefix) {
    Expect(ExtTextOutW(dc, 0, 0, ETO_IGNORELANGUAGE, nullptr, L"1.", 2, nullptr) != FALSE,
           "rasterize the GDI prefix");
  }
  Expect(ExtTextOutW(dc, sample.prefix_slot, 0, ETO_IGNORELANGUAGE, nullptr, text.data(),
                     static_cast<UINT>(text.size()), nullptr) != FALSE,
         "rasterize the GDI text at the measured prefix slot");
  GdiFlush();
  sample.ink_pixels = static_cast<std::size_t>(
      std::count_if(pixels, pixels + count, [](std::uint32_t pixel) { return pixel != 0; }));
  Expect(sample.ink_pixels != 0, "the actual GDI mask must contain glyph coverage");
  for (int y = 0; y < sample.cell_height; ++y) {
    for (int x = 0; x < sample.mask_width; ++x) {
      if (pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(sample.mask_width) +
                 static_cast<std::size_t>(x)] != 0) {
        sample.rightmost_ink = std::max(sample.rightmost_ink, x);
      }
    }
  }
  Expect(sample.rightmost_ink >= 0 && sample.rightmost_ink < sample.mask_width,
         "glyph coverage must remain inside the measured GDI mask bounds");

  SelectObject(dc, old_bitmap);
  SelectObject(dc, old_font);
  DeleteObject(bitmap);
  DeleteObject(font);
  DeleteDC(dc);
  return sample;
}

}  // namespace

int main() {
  using ziliu::ui::detail::ResolveSogouGdiRasterScale;

  for (const auto [dpi, expected_font_pixels] :
       {std::pair{96.0F, 24}, std::pair{144.0F, 36}, std::pair{192.0F, 48}}) {
    const auto raster = ResolveSogouGdiRasterScale(dpi);
    Expect(raster.ToPhysicalPixels(24.0F) == expected_font_pixels,
           "SSF mask font height must follow render-target DPI");
    ExpectNear(raster.ToLogicalPixels(expected_font_pixels), 24.0F,
               "higher-resolution SSF masks must retain the 24-DIP layout height");
    ExpectNear(raster.ToLogicalPixels(raster.ToPhysicalPixels(2.0F)), 2.0F,
               "the accepted two-DIP number separation must remain stable");
  }

  const auto latin96 = SampleMask(96.0F, L"Consolas", L"ziliu'shu'ru'fa", false);
  const auto latin144 = SampleMask(144.0F, L"Consolas", L"ziliu'shu'ru'fa", false);
  const auto latin192 = SampleMask(192.0F, L"Consolas", L"ziliu'shu'ru'fa", false);
  const auto candidate96 = SampleMask(96.0F, L"Microsoft YaHei", L"输入法", true);
  const auto candidate144 = SampleMask(144.0F, L"Microsoft YaHei", L"输入法", true);
  const auto candidate192 = SampleMask(192.0F, L"Microsoft YaHei", L"输入法", true);
  std::cout << "GDI_MASK_METRICS latin=" << latin96.text_width << ',' << latin144.text_width
            << ',' << latin192.text_width << " candidate=" << candidate96.mask_width << ','
            << candidate144.mask_width << ',' << candidate192.mask_width << '\n';

  Expect(latin96.cell_height < latin144.cell_height &&
             latin144.cell_height < latin192.cell_height,
         "real preedit mask height must increase at 96/144/192 DPI");
  Expect(candidate96.mask_width < candidate144.mask_width &&
             candidate144.mask_width < candidate192.mask_width,
         "real prefixed Chinese mask width must increase at 96/144/192 DPI");
  for (const auto [dpi, latin, candidate] :
       {std::tuple{96.0F, latin96, candidate96}, std::tuple{144.0F, latin144, candidate144},
        std::tuple{192.0F, latin192, candidate192}}) {
    const auto raster = ResolveSogouGdiRasterScale(dpi);
    Expect(std::abs(raster.ToLogicalPixels(latin.text_width) -
                    static_cast<float>(latin96.text_width)) <= 6.0F,
           "real preedit measurement must remain stable in logical pixels");
    Expect(std::abs(raster.ToLogicalPixels(candidate.mask_width) -
                    static_cast<float>(candidate96.mask_width)) <= 1.0F,
           "real candidate plus prefix must remain stable in logical pixels");
    Expect(candidate.prefix_slot > raster.ToPhysicalPixels(2.0F) &&
               candidate.prefix_slot < candidate.mask_width,
           "real prefix slot must retain label advance plus the two-DIP gap");
    Expect(candidate.prefix_slot - candidate.prefix_width == raster.ToPhysicalPixels(2.0F),
           "real prefix slot must preserve the exact two-DIP separation");
  }

  const auto invalid = ResolveSogouGdiRasterScale(NAN);
  ExpectNear(invalid.dpi, 96.0F, "invalid target DPI must preserve the 96-DPI path");
  return EXIT_SUCCESS;
}
