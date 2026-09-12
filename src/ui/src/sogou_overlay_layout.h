#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace ziliu::ui::detail {

struct OverlayBounds {
  float left = 0.0F;
  float top = 0.0F;
  float right = 0.0F;
  float bottom = 0.0F;

  bool operator==(const OverlayBounds&) const = default;
};

inline constexpr std::array<std::int32_t, 10>
    kSogouBottomRightEdgeAlign{0, 0, 0, 0, 0, 2, 0, 2, 6, 0};

[[nodiscard]] constexpr bool UsesSogouBottomRightEdgeAlign(
    const std::array<std::int32_t, 10>& align) noexcept {
  return align == kSogouBottomRightEdgeAlign;
}

[[nodiscard]] constexpr OverlayBounds ResolveSogouOverlayBounds(
    const std::array<std::int32_t, 10>& align, float surface_width,
    float surface_height, float bitmap_width, float bitmap_height,
    float destination_scale) {
  const float scale = std::max(destination_scale, 0.0F);
  float left = 0.0F;
  float top = 0.0F;
  float width = std::max(bitmap_width * scale, 0.0F);
  float height = std::max(bitmap_height * scale, 0.0F);

  // This tuple is used by the pinned 636332 V1 skin and by its matching H2/V2
  // assets. A real Sogou 16.6 capture proves that it anchors the bitmap flush
  // to the surface's right and bottom edges. The 2 values select the anchor;
  // they are not pixel insets.
  if (UsesSogouBottomRightEdgeAlign(align)) {
    left = std::max(surface_width - width, 0.0F);
    top = std::max(surface_height - height, 0.0F);
    return OverlayBounds{left, top, left + width, top + height};
  }

  // Retain the documented legacy x/y[/width/height] subset. Unknown extended
  // tuples stay at the canvas origin instead of guessing at undocumented
  // anchors.
  bool simple_tuple = true;
  for (std::size_t index = 4; index < align.size(); ++index) {
    simple_tuple = simple_tuple && align[index] == 0;
  }
  if (simple_tuple) {
    left = static_cast<float>(align[0]) * scale;
    top = static_cast<float>(align[1]) * scale;
    if (align[2] > 0 && align[3] > 0) {
      width = static_cast<float>(align[2]) * scale;
      height = static_cast<float>(align[3]) * scale;
    }
  }
  return OverlayBounds{left, top, left + width, top + height};
}

}  // namespace ziliu::ui::detail
