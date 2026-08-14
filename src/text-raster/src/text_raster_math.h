#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>

namespace ziliu::text_raster::detail {

constexpr bool IsUnicodeScalar(std::uint32_t value) noexcept {
  return value <= 0x10FFFFU && !(value >= 0xD800U && value <= 0xDFFFU);
}

constexpr std::uint8_t DivideByteProductBy255(std::uint8_t left,
                                               std::uint8_t right) noexcept {
  return static_cast<std::uint8_t>(
      (static_cast<std::uint32_t>(left) * right) / 255U);
}

constexpr std::uint8_t SaturatingByteAdd(std::uint8_t left,
                                         std::uint8_t right) noexcept {
  const auto sum = static_cast<std::uint32_t>(left) + right;
  return static_cast<std::uint8_t>(
      std::min(sum, static_cast<std::uint32_t>(
                        std::numeric_limits<std::uint8_t>::max())));
}

struct PremultipliedBgra {
  std::uint8_t blue = 0;
  std::uint8_t green = 0;
  std::uint8_t red = 0;
  std::uint8_t alpha = 0;
};

constexpr PremultipliedBgra ResolveGlyphPixel(std::uint32_t color_argb,
                                              std::uint8_t coverage) noexcept {
  const auto color_alpha = static_cast<std::uint8_t>(color_argb >> 24U);
  const auto alpha = DivideByteProductBy255(color_alpha, coverage);
  return {
      .blue = DivideByteProductBy255(
          static_cast<std::uint8_t>(color_argb), alpha),
      .green = DivideByteProductBy255(
          static_cast<std::uint8_t>(color_argb >> 8U), alpha),
      .red = DivideByteProductBy255(
          static_cast<std::uint8_t>(color_argb >> 16U), alpha),
      .alpha = alpha,
  };
}

constexpr PremultipliedBgra SourceOver(PremultipliedBgra source,
                                       PremultipliedBgra destination) noexcept {
  const auto inverse_source_alpha =
      static_cast<std::uint8_t>(255U - source.alpha);
  return {
      .blue = SaturatingByteAdd(
          source.blue,
          DivideByteProductBy255(destination.blue, inverse_source_alpha)),
      .green = SaturatingByteAdd(
          source.green,
          DivideByteProductBy255(destination.green, inverse_source_alpha)),
      .red = SaturatingByteAdd(
          source.red,
          DivideByteProductBy255(destination.red, inverse_source_alpha)),
      .alpha = SaturatingByteAdd(
          source.alpha,
          DivideByteProductBy255(destination.alpha, inverse_source_alpha)),
  };
}

constexpr bool FitsBuffer(std::uint32_t width, std::uint32_t height,
                          std::uint32_t stride,
                          std::uint32_t capacity) noexcept {
  if (stride < width) {
    return false;
  }
  const auto required = static_cast<std::uint64_t>(stride) * height;
  return required <= capacity;
}

}  // namespace ziliu::text_raster::detail
