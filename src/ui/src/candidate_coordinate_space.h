#pragma once

#include <algorithm>
#include <cmath>

namespace ziliu::ui::detail {

inline constexpr float kDefaultDpi = 96.0F;

[[nodiscard]] constexpr float ResolveCandidateCoordinateScale(
    bool uses_custom_rendering, bool custom_theme_scale_with_windows,
    float system_dpi_scale) noexcept {
  if (uses_custom_rendering && !custom_theme_scale_with_windows) {
    // A custom theme can opt out of monitor scaling. Its layout, raster target,
    // window size, and hit-test coordinates then share one 96-DPI space.
    return 1.0F;
  }
  return std::max(system_dpi_scale, 1.0F);
}

[[nodiscard]] constexpr float CandidateRenderDpi(
    float coordinate_scale) noexcept {
  return kDefaultDpi * std::max(coordinate_scale, 1.0F);
}

[[nodiscard]] inline int CandidateCoordinateToPixels(
    float value, float coordinate_scale) noexcept {
  return static_cast<int>(
      std::ceil(value * std::max(coordinate_scale, 1.0F)));
}

[[nodiscard]] constexpr float CandidatePixelsToCoordinate(
    int value, float coordinate_scale) noexcept {
  return static_cast<float>(value) /
         std::max(coordinate_scale, 1.0F);
}

}  // namespace ziliu::ui::detail
