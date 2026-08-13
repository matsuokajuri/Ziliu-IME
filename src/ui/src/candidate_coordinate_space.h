#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ziliu::ui::detail {

inline constexpr float kDefaultDpi = 96.0F;

struct CandidatePixelPoint {
  int x = 0;
  int y = 0;

  bool operator==(const CandidatePixelPoint&) const = default;
};

struct ThemeBitmapLogicalSize {
  float width = 0.0F;
  float height = 0.0F;

  bool operator==(const ThemeBitmapLogicalSize&) const = default;
};

[[nodiscard]] constexpr std::uint32_t ResolveCandidateTargetDpi(
    std::uint32_t monitor_dpi, std::uint32_t window_dpi) noexcept {
  const std::uint32_t resolved = monitor_dpi != 0 ? monitor_dpi : window_dpi;
  return std::max(resolved, static_cast<std::uint32_t>(kDefaultDpi));
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
  return static_cast<float>(value) / std::max(coordinate_scale, 1.0F);
}

[[nodiscard]] constexpr float ResolveThemeUnitScale(
    float base_dpi, float layout_scale) noexcept {
  return kDefaultDpi / std::max(base_dpi, 1.0F) *
         std::max(layout_scale, 0.0F);
}

[[nodiscard]] constexpr ThemeBitmapLogicalSize ResolveThemeBitmapLogicalSize(
    std::uint32_t pixel_width, std::uint32_t pixel_height, float base_dpi,
    float layout_scale) noexcept {
  const float theme_unit_scale = ResolveThemeUnitScale(base_dpi, layout_scale);
  return {
      static_cast<float>(pixel_width) * theme_unit_scale,
      static_cast<float>(pixel_height) * theme_unit_scale,
  };
}

[[nodiscard]] inline int CandidateThemeOffsetToPixels(
    std::int32_t value, float theme_unit_scale,
    float coordinate_scale) noexcept {
  return static_cast<int>(std::lround(
      static_cast<float>(value) * std::max(theme_unit_scale, 0.0F) *
      std::max(coordinate_scale, 1.0F)));
}

[[nodiscard]] inline CandidatePixelPoint ClampCandidateWindowOrigin(
    CandidatePixelPoint desired_origin, int window_width, int window_height,
    int input_spot_top, int work_area_left, int work_area_top,
    int work_area_right, int work_area_bottom) noexcept {
  const int available_width = std::max(work_area_right - work_area_left, 0);
  const int available_height = std::max(work_area_bottom - work_area_top, 0);
  const int fitted_width = std::clamp(window_width, 0, available_width);
  const int fitted_height = std::clamp(window_height, 0, available_height);
  const int maximum_x =
      std::max(work_area_right - fitted_width, work_area_left);
  const int maximum_y =
      std::max(work_area_bottom - fitted_height, work_area_top);

  const int x = std::clamp(desired_origin.x, work_area_left, maximum_x);
  int y = desired_origin.y;
  if (y > maximum_y) {
    y = input_spot_top - fitted_height - 2;
  }
  y = std::clamp(y, work_area_top, maximum_y);
  return {x, y};
}

[[nodiscard]] constexpr float ResolveCandidateLayoutScale(
    float theme_font_size, float effective_font_size, bool scale_with_text,
    bool has_font_size_override) noexcept {
  if (!scale_with_text || !has_font_size_override) {
    return 1.0F;
  }
  const float safe_theme_size = std::max(theme_font_size, 1.0F);
  return std::clamp(effective_font_size / safe_theme_size, 0.50F, 2.00F);
}

}  // namespace ziliu::ui::detail
