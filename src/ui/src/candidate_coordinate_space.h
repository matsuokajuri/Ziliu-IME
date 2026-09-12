#pragma once

#include <algorithm>
#include <cmath>

namespace ziliu::ui::detail {

inline constexpr float kDefaultDpi = 96.0F;

[[nodiscard]] constexpr float ResolveCandidateCoordinateScale(
    bool uses_sogou_rendering, float system_dpi_scale) noexcept {
  if (uses_sogou_rendering) {
    // Legacy SSF measurements are physical pixels. Sogou keeps the skin,
    // text, and hit-test coordinates in a 96-DPI space even on a HiDPI
    // monitor, so applying the system scale a second time changes the skin.
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
