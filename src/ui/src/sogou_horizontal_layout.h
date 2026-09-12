#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <string_view>

namespace ziliu::ui::detail {

inline constexpr float kSogouHorizontalCandidatePadding = 15.0F;
inline constexpr float kSogouHorizontalMinimumCandidateWidth = 57.0F;
inline constexpr float kSogouHorizontalActionStripWidth = 90.0F;
inline constexpr float kSogouVerticalPreeditExtraHeight = 4.0F;
inline constexpr float kSogouVerticalCandidateLeading = 10.0F;
inline constexpr float kSogouVerticalPagerExtraHeight = 14.0F;
inline constexpr float kSogouVerticalPagerDisabledOpacity = 0.31F;

struct SogouHorizontalBounds {
  float left = 0.0F;
  float top = 0.0F;
  float right = 0.0F;
  float bottom = 0.0F;

  bool operator==(const SogouHorizontalBounds&) const = default;
};

struct SogouHorizontalActionLayout {
  SogouHorizontalBounds strip;
  SogouHorizontalBounds previous_hit_target;
  SogouHorizontalBounds next_expand_hit_target;
  SogouHorizontalBounds separator;
  SogouHorizontalBounds menu_hit_target;
  SogouHorizontalBounds next_expand_icon;
  SogouHorizontalBounds menu_line_top;
  SogouHorizontalBounds menu_line_middle;
  SogouHorizontalBounds menu_line_bottom;

  bool operator==(const SogouHorizontalActionLayout&) const = default;
};

struct SogouVerticalPagerLayout {
  SogouHorizontalBounds previous_icon;
  SogouHorizontalBounds next_icon;

  bool operator==(const SogouVerticalPagerLayout&) const = default;
};

[[nodiscard]] inline std::wstring BuildSogouHorizontalCandidateLabel(
    std::size_t one_based_index, std::wstring_view text) {
  return std::to_wstring(one_based_index) + L"." + std::wstring(text);
}

[[nodiscard]] inline std::wstring BuildSogouVerticalCandidateLabel(
    std::size_t one_based_index, std::wstring_view text) {
  return BuildSogouHorizontalCandidateLabel(one_based_index, text);
}

[[nodiscard]] constexpr float ResolveSogouHorizontalCandidateWidth(
    float measured_mixed_font_width, float layout_scale = 1.0F) noexcept {
  const float scale = std::max(layout_scale, 0.0F);
  return std::max(measured_mixed_font_width +
                      kSogouHorizontalCandidatePadding * scale,
                  kSogouHorizontalMinimumCandidateWidth * scale);
}

[[nodiscard]] constexpr float SogouHorizontalActionStripWidth(
    float layout_scale = 1.0F) noexcept {
  return kSogouHorizontalActionStripWidth *
         std::max(layout_scale, 0.0F);
}

[[nodiscard]] constexpr float ResolveSogouPreeditDWriteFontSize(
    float configured_font_size) noexcept {
  // DirectWrite's natural Arial raster is one pixel taller than the requested
  // em size at the SSF reference DPI.
  return std::max(configured_font_size - 1.0F, 1.0F);
}

[[nodiscard]] constexpr float ResolveSogouPreeditCaretDWriteFontSize(
    float configured_font_size) noexcept {
  // Sogou's GDI advance is narrower than its visible glyph raster. Keep a
  // dedicated natural-metrics format so the caret does not drift as the
  // visible preedit font is calibrated independently.
  return std::max(configured_font_size - 2.0F, 1.0F);
}

[[nodiscard]] constexpr float ResolveSogouHorizontalCandidateTextTop(
    float row_top,
    float layout_scale = 1.0F) noexcept {
  return row_top - 2.0F * std::max(layout_scale, 0.0F);
}

[[nodiscard]] constexpr float ResolveSogouHorizontalPreeditTextTop(
    float content_top,
    float layout_scale = 1.0F) noexcept {
  return content_top + 2.0F * std::max(layout_scale, 0.0F);
}

[[nodiscard]] constexpr float ResolveSogouVerticalPreeditHeight(
    float content_top, float configured_font_size, float content_bottom,
    float layout_scale = 1.0F) noexcept {
  const float scale = std::max(layout_scale, 0.0F);
  return content_top + configured_font_size + content_bottom +
         kSogouVerticalPreeditExtraHeight * scale;
}

[[nodiscard]] constexpr float ResolveSogouVerticalCandidateRowHeight(
    float configured_font_size, float layout_scale = 1.0F) noexcept {
  return std::max(configured_font_size +
                      kSogouVerticalCandidateLeading *
                          std::max(layout_scale, 0.0F),
                  1.0F);
}

[[nodiscard]] constexpr float ResolveSogouVerticalCandidateTextTop(
    float row_top, float layout_scale = 1.0F) noexcept {
  static_cast<void>(layout_scale);
  return row_top;
}

[[nodiscard]] constexpr float ResolveSogouVerticalPreeditTextTop(
    float content_top, float layout_scale = 1.0F) noexcept {
  return ResolveSogouHorizontalPreeditTextTop(content_top, layout_scale);
}

[[nodiscard]] constexpr float ResolveSogouVerticalSurfaceWidth(
    float content_width, float candidate_left, float widest_label_width,
    float right_anchored_overlay_width) noexcept {
  return std::max(
      content_width,
      candidate_left + widest_label_width + right_anchored_overlay_width);
}

[[nodiscard]] constexpr float ResolveSogouVerticalSurfaceHeight(
    float content_height, bool has_fallback_pager,
    float layout_scale = 1.0F) noexcept {
  return content_height +
         (has_fallback_pager
              ? kSogouVerticalPagerExtraHeight *
                    std::max(layout_scale, 0.0F)
              : 0.0F);
}

[[nodiscard]] inline SogouHorizontalBounds
ResolveSogouHorizontalPreeditCaretBounds(float content_left,
                                         float measured_text_width,
                                         float content_top,
                                         float preedit_bottom,
                                         float layout_scale = 1.0F) noexcept {
  const float scale = std::max(layout_scale, 0.0F);
  const float caret_left =
      std::round(content_left + measured_text_width + 2.0F * scale);
  return {
      caret_left,
      content_top + 2.0F * scale,
      caret_left + std::max(scale, 1.0F),
      preedit_bottom + 3.0F * scale,
  };
}

[[nodiscard]] inline SogouHorizontalBounds
ResolveSogouVerticalPreeditCaretBounds(float content_left,
                                       float measured_text_width,
                                       float content_top,
                                       float configured_font_size,
                                       float layout_scale = 1.0F) noexcept {
  const float scale = std::max(layout_scale, 0.0F);
  const float caret_left =
      std::round(content_left + measured_text_width + 2.0F * scale);
  return {
      caret_left,
      content_top + 2.0F * scale,
      caret_left + std::max(scale, 1.0F),
      content_top + configured_font_size + 3.0F * scale,
  };
}

[[nodiscard]] constexpr SogouVerticalPagerLayout
ResolveSogouVerticalPagerLayout(float candidate_left,
                                float candidate_rows_bottom,
                                float layout_scale = 1.0F) noexcept {
  const float scale = std::max(layout_scale, 0.0F);
  const float top = candidate_rows_bottom + 3.0F * scale;
  const float bottom = top + 11.0F * scale;
  return {
      {candidate_left, top, candidate_left + 6.0F * scale, bottom},
      {candidate_left + 12.0F * scale, top,
       candidate_left + 18.0F * scale, bottom},
  };
}

[[nodiscard]] constexpr SogouHorizontalActionLayout
ResolveSogouHorizontalActionLayout(float window_width,
                                   float candidate_right_inset,
                                   float candidate_row_top,
                                   float candidate_row_height,
                                   float layout_scale = 1.0F) noexcept {
  const float scale = std::max(layout_scale, 0.0F);
  const float strip_right =
      std::max(window_width - candidate_right_inset, 0.0F);
  const float strip_left =
      std::max(strip_right - kSogouHorizontalActionStripWidth * scale,
               0.0F);
  const float row_bottom = candidate_row_top + candidate_row_height;
  return {
      {strip_left, candidate_row_top, strip_right, row_bottom},
      {strip_left, candidate_row_top, strip_left + 30.0F * scale,
       row_bottom},
      {strip_left + 30.0F * scale, candidate_row_top,
       strip_left + 63.0F * scale, row_bottom},
      {strip_left + 63.0F * scale, candidate_row_top + 7.0F * scale,
       strip_left + 64.0F * scale, candidate_row_top + 27.0F * scale},
      {strip_left + 64.0F * scale, candidate_row_top, strip_right,
       row_bottom},
      {strip_left + 42.0F * scale, candidate_row_top + 9.0F * scale,
       strip_left + 57.0F * scale, candidate_row_top + 24.0F * scale},
      {strip_left + 72.0F * scale, candidate_row_top + 11.0F * scale,
       strip_left + 88.0F * scale, candidate_row_top + 13.0F * scale},
      {strip_left + 72.0F * scale, candidate_row_top + 16.0F * scale,
       strip_left + 88.0F * scale, candidate_row_top + 18.0F * scale},
      {strip_left + 72.0F * scale, candidate_row_top + 21.0F * scale,
       strip_left + 88.0F * scale, candidate_row_top + 23.0F * scale},
  };
}

[[nodiscard]] constexpr bool UseNearestNeighborForThemeBitmap(
    bool uses_sogou_rendering) noexcept {
  return uses_sogou_rendering;
}

}  // namespace ziliu::ui::detail
