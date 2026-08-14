#include <ziliu/text_raster/text_raster_api.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace {

class RasterHandle final {
 public:
  RasterHandle() = default;
  RasterHandle(const RasterHandle&) = delete;
  RasterHandle& operator=(const RasterHandle&) = delete;
  ~RasterHandle() { ZiliuTextRasterDestroy(value); }

  ZiliuTextRasterHandle value = nullptr;
};

bool ContainsInk(const std::vector<std::uint8_t>& coverage) {
  return std::ranges::any_of(coverage,
                             [](std::uint8_t value) { return value != 0U; });
}

bool IsPremultiplied(const std::vector<std::uint8_t>& pixels) {
  for (std::size_t offset = 0; offset + 3U < pixels.size(); offset += 4U) {
    const std::uint8_t alpha = pixels[offset + 3U];
    if (pixels[offset] > alpha || pixels[offset + 1U] > alpha ||
        pixels[offset + 2U] > alpha) {
      return false;
    }
  }
  return true;
}

}  // namespace

int main() {
  if (ZiliuTextRasterGetAbiVersion() != ZILIU_TEXT_RASTER_ABI_VERSION) {
    return 1;
  }

  RasterHandle raster;
  if (ZiliuTextRasterCreate(&raster.value) !=
          ZILIU_TEXT_RASTER_STATUS_OK ||
      raster.value == nullptr) {
    return 2;
  }

  const ZiliuTextRasterFontConfig config{
      .struct_size = sizeof(ZiliuTextRasterFontConfig),
      .family_name = L"Segoe UI",
      .weight = 400U,
      .stretch = 5U,
      .style = ZILIU_TEXT_RASTER_FONT_STYLE_NORMAL,
      .pixel_width = 0U,
      .pixel_height = 20U,
  };
  if (ZiliuTextRasterConfigureFont(raster.value, &config) !=
      ZILIU_TEXT_RASTER_STATUS_OK) {
    return 3;
  }

  constexpr wchar_t kValidText[] = L"A\U0001F600z";
  std::uint32_t scalar_count = 0U;
  if (ZiliuTextRasterDecodeUtf16(
          kValidText, static_cast<std::uint32_t>(std::size(kValidText) - 1U),
          ZILIU_TEXT_RASTER_INVALID_UTF16_REJECT, nullptr, 0U,
          &scalar_count) != ZILIU_TEXT_RASTER_STATUS_BUFFER_TOO_SMALL ||
      scalar_count != 3U) {
    return 4;
  }
  std::array<std::uint32_t, 3> scalars{};
  if (ZiliuTextRasterDecodeUtf16(
          kValidText, static_cast<std::uint32_t>(std::size(kValidText) - 1U),
          ZILIU_TEXT_RASTER_INVALID_UTF16_REJECT, scalars.data(),
          static_cast<std::uint32_t>(scalars.size()), &scalar_count) !=
          ZILIU_TEXT_RASTER_STATUS_OK ||
      scalars != std::array<std::uint32_t, 3>{0x41U, 0x1F600U, 0x7AU}) {
    return 5;
  }

  constexpr wchar_t kInvalidText[] = {static_cast<wchar_t>(0xD800U), L'X'};
  if (ZiliuTextRasterDecodeUtf16(
          kInvalidText, static_cast<std::uint32_t>(std::size(kInvalidText)),
          ZILIU_TEXT_RASTER_INVALID_UTF16_REJECT, scalars.data(),
          static_cast<std::uint32_t>(scalars.size()), &scalar_count) !=
      ZILIU_TEXT_RASTER_STATUS_INVALID_UTF16) {
    return 6;
  }
  if (ZiliuTextRasterDecodeUtf16(
          kInvalidText, static_cast<std::uint32_t>(std::size(kInvalidText)),
          ZILIU_TEXT_RASTER_INVALID_UTF16_REPLACE, scalars.data(),
          static_cast<std::uint32_t>(scalars.size()), &scalar_count) !=
          ZILIU_TEXT_RASTER_STATUS_OK ||
      scalar_count != 2U || scalars[0] != 0xFFFDU || scalars[1] != 0x58U) {
    return 7;
  }

  std::uint32_t glyph_index = 0U;
  if (ZiliuTextRasterLookupGlyph(raster.value, 0x41U, &glyph_index) !=
          ZILIU_TEXT_RASTER_STATUS_OK ||
      glyph_index == 0U) {
    return 8;
  }
  std::uint32_t missing_index = 99U;
  if (ZiliuTextRasterLookupGlyph(raster.value, 0x10FFFFU, &missing_index) !=
          ZILIU_TEXT_RASTER_STATUS_GLYPH_NOT_FOUND ||
      missing_index != 0U) {
    return 9;
  }

  const ZiliuTextRasterGlyphOptions options{
      .struct_size = sizeof(ZiliuTextRasterGlyphOptions),
      .hinting = ZILIU_TEXT_RASTER_HINTING_NONE,
      .hinting_target = ZILIU_TEXT_RASTER_TARGET_NORMAL,
      .render_mode = ZILIU_TEXT_RASTER_RENDER_NORMAL,
  };
  ZiliuTextRasterGlyphMetrics metrics{
      .struct_size = sizeof(ZiliuTextRasterGlyphMetrics),
  };
  ZiliuTextRasterCoverageBitmap bitmap{
      .struct_size = sizeof(ZiliuTextRasterCoverageBitmap),
  };
  if (ZiliuTextRasterRenderGlyph(raster.value, glyph_index, &options,
                                 &metrics, &bitmap) !=
          ZILIU_TEXT_RASTER_STATUS_BUFFER_TOO_SMALL ||
      metrics.glyph_index != glyph_index || bitmap.width == 0U ||
      bitmap.height == 0U) {
    return 10;
  }

  bitmap.stride_bytes = bitmap.width;
  bitmap.capacity_bytes = bitmap.width * bitmap.height;
  std::vector<std::uint8_t> coverage(bitmap.capacity_bytes, 0U);
  bitmap.pixels = coverage.data();
  if (ZiliuTextRasterRenderGlyph(raster.value, glyph_index, &options,
                                 &metrics, &bitmap) !=
          ZILIU_TEXT_RASTER_STATUS_OK ||
      !ContainsInk(coverage)) {
    return 11;
  }

  const std::uint32_t surface_width = bitmap.width + 2U;
  const std::uint32_t surface_height = bitmap.height + 2U;
  const std::uint32_t surface_stride = surface_width * 4U;
  std::vector<std::uint8_t> pixels(
      static_cast<std::size_t>(surface_stride) * surface_height, 0U);
  ZiliuTextRasterBgraSurface surface{
      .struct_size = sizeof(ZiliuTextRasterBgraSurface),
      .pixels = pixels.data(),
      .capacity_bytes = static_cast<std::uint32_t>(pixels.size()),
      .width = surface_width,
      .height = surface_height,
      .stride_bytes = surface_stride,
  };
  if (ZiliuTextRasterCompositeCoverageBgra(
          &bitmap, 0x80804020U, 1, 1, &surface) !=
          ZILIU_TEXT_RASTER_STATUS_OK ||
      !IsPremultiplied(pixels) ||
      std::ranges::none_of(
          pixels, [](std::uint8_t value) { return value != 0U; })) {
    return 12;
  }

  if (ZiliuTextRasterCompositeCoverageBgra(
          &bitmap, 0xFFFFFFFFU, -static_cast<std::int32_t>(bitmap.width), 0,
          &surface) != ZILIU_TEXT_RASTER_STATUS_OK) {
    return 13;
  }

  return 0;
}
