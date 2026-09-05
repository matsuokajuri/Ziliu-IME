#pragma once

#include "ziliu/ui/bitmap.h"
#include "sogou_surface_scaler.h"

#include <new>
#include <utility>

namespace ziliu::ui::detail {

enum class BitmapSurfaceScale {
  kUnscaled,
  kMitchell2x,
};

struct SogouSurfaceBitmap {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint32_t stride = 0;
  // Owned, tightly packed premultiplied BGRA in the scaler's bottom-up order.
  std::vector<SogouPbgra8> pixels;
};

// Format adapter only. The caller must select a scaling capability explicitly;
// this function does not choose a Sogou DPI or layout policy. Unscaled alpha
// conversion uses the existing scaler's integer channel * alpha / 255 rule.
// Failure leaves output untouched, including for malformed public bitmap data.
[[nodiscard]] inline bool PrepareSogouBitmapSurface(
    const RgbaBitmap& source, BitmapSurfaceScale scale, SogouSurfaceBitmap* output,
    std::uint64_t maximum_output_pixels = 16U * 1024U * 1024U) {
  if (output == nullptr || source.width == 0U || source.height == 0U ||
      maximum_output_pixels == 0U ||
      (scale != BitmapSurfaceScale::kUnscaled && scale != BitmapSurfaceScale::kMitchell2x)) {
    return false;
  }
  static_assert(sizeof(SogouPbgra8) == 4U);
  const std::uint32_t factor = scale == BitmapSurfaceScale::kMitchell2x ? 2U : 1U;
  const std::uint64_t count = static_cast<std::uint64_t>(source.width) * source.height;
  if (static_cast<std::uint64_t>(source.width) * 4U != source.stride ||
      count > (std::numeric_limits<std::size_t>::max)() / 4U ||
      count * 4U != source.pixels.size() ||
      count > maximum_output_pixels / (factor * factor) ||
      count > (std::numeric_limits<std::size_t>::max)() / (4U * factor * factor) ||
      source.width > (std::numeric_limits<std::uint32_t>::max)() / (4U * factor) ||
      source.height > (std::numeric_limits<std::uint32_t>::max)() / factor) {
    return false;
  }
  try {
    SogouSurfaceBitmap prepared;
    prepared.width = source.width * factor;
    prepared.height = source.height * factor;
    prepared.stride = prepared.width * 4U;
    if (scale == BitmapSurfaceScale::kMitchell2x) {
      std::vector<SogouRgba8> rgba(static_cast<std::size_t>(count));
      for (std::size_t index = 0; index < rgba.size(); ++index) {
        const std::size_t offset = index * 4U;
        rgba[index] = {source.pixels[offset], source.pixels[offset + 1U],
                       source.pixels[offset + 2U], source.pixels[offset + 3U]};
      }
      if (!ScaleSogouRgbaMitchell2x(rgba, source.width, source.height, &prepared.pixels)) {
        return false;
      }
    } else {
      prepared.pixels.resize(static_cast<std::size_t>(count));
      for (std::uint32_t y = 0; y < source.height; ++y) {
        for (std::uint32_t x = 0; x < source.width; ++x) {
          const std::size_t offset =
              (static_cast<std::size_t>(y) * source.width + x) * 4U;
          const std::uint8_t alpha = source.pixels[offset + 3U];
          const auto premultiply = [alpha](std::uint8_t channel) {
            return static_cast<std::uint8_t>(static_cast<std::uint32_t>(channel) * alpha / 255U);
          };
          prepared.pixels[static_cast<std::size_t>(source.height - 1U - y) * source.width + x] =
              {premultiply(source.pixels[offset + 2U]),
               premultiply(source.pixels[offset + 1U]),
               premultiply(source.pixels[offset]), alpha};
        }
      }
    }
    *output = std::move(prepared);
    return true;
  } catch (const std::bad_alloc&) {
    return false;
  }
}

}  // namespace ziliu::ui::detail
