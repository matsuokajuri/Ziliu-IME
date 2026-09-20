#pragma once

#include <algorithm>
#include <cmath>

namespace ziliu::ui::detail {

inline constexpr float kSogouGdiBaseDpi = 96.0F;

struct SogouGdiRasterScale {
  float dpi = kSogouGdiBaseDpi;
  float scale = 1.0F;

  [[nodiscard]] int ToPhysicalPixels(float logical_pixels) const noexcept {
    return std::max(1, static_cast<int>(std::lround(logical_pixels * scale)));
  }

  [[nodiscard]] float ToLogicalPixels(int physical_pixels) const noexcept {
    return static_cast<float>(physical_pixels) / scale;
  }
};

[[nodiscard]] inline SogouGdiRasterScale ResolveSogouGdiRasterScale(
    float render_dpi) noexcept {
  const float dpi = std::isfinite(render_dpi)
                        ? std::max(render_dpi, kSogouGdiBaseDpi)
                        : kSogouGdiBaseDpi;
  return SogouGdiRasterScale{dpi, dpi / kSogouGdiBaseDpi};
}

}  // namespace ziliu::ui::detail
