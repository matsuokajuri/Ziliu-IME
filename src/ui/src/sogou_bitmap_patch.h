#pragma once

#include "ziliu/core/theme_manifest.h"

#include <d2d1.h>
#include <d2d1helper.h>

#include <algorithm>
#include <cmath>

namespace ziliu::ui::detail {

[[nodiscard]] inline float SnapBitmapPatchCoordinate(
    ID2D1RenderTarget* render_target, float coordinate, bool horizontal) noexcept {
  if (render_target == nullptr || !std::isfinite(coordinate)) return coordinate;
  float dpi_x = 96.0F;
  float dpi_y = 96.0F;
  render_target->GetDpi(&dpi_x, &dpi_y);
  D2D1_MATRIX_3X2_F transform{};
  render_target->GetTransform(&transform);
  const bool axis_aligned = transform._12 == 0.0F && transform._21 == 0.0F;
  const float scale = horizontal ? transform._11 : transform._22;
  const float translation = horizontal ? transform._31 : transform._32;
  const float dpi = horizontal ? dpi_x : dpi_y;
  if (!axis_aligned || !std::isfinite(scale) || !std::isfinite(translation) ||
      !std::isfinite(dpi) || scale == 0.0F || dpi <= 0.0F) {
    return coordinate;
  }
  const float physical = (coordinate * scale + translation) * dpi / 96.0F;
  return (std::round(physical) * 96.0F / dpi - translation) / scale;
}

inline void DrawSogouBitmapPatch(ID2D1RenderTarget* render_target, ID2D1Bitmap* bitmap,
                                 const D2D1_RECT_F& source,
                                 const D2D1_RECT_F& destination,
                                 core::ThemeImageLayout horizontal_layout,
                                 core::ThemeImageLayout vertical_layout,
                                 float destination_scale) {
  if (render_target == nullptr || bitmap == nullptr || source.right <= source.left ||
      source.bottom <= source.top || destination.right <= destination.left ||
      destination.bottom <= destination.top) {
    return;
  }

  const float source_width = source.right - source.left;
  const float source_height = source.bottom - source.top;
  const float natural_width = std::max(source_width * destination_scale, 0.5F);
  const float natural_height = std::max(source_height * destination_scale, 0.5F);
  const bool tile_horizontal = horizontal_layout == core::ThemeImageLayout::kTile;
  const bool tile_vertical = vertical_layout == core::ThemeImageLayout::kTile;
  const bool fixed_horizontal = horizontal_layout == core::ThemeImageLayout::kFixed;
  const bool fixed_vertical = vertical_layout == core::ThemeImageLayout::kFixed;
  const float horizontal_step =
      tile_horizontal || fixed_horizontal ? natural_width
                                          : destination.right - destination.left;
  const float vertical_step =
      tile_vertical || fixed_vertical ? natural_height
                                      : destination.bottom - destination.top;

  const D2D1_ANTIALIAS_MODE previous_antialias_mode = render_target->GetAntialiasMode();
  render_target->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
  for (float top = destination.top; top < destination.bottom; top += vertical_step) {
    const float drawn_height = std::min(vertical_step, destination.bottom - top);
    const float source_drawn_height =
        tile_vertical || fixed_vertical
            ? source_height * drawn_height / natural_height
            : source_height;
    for (float left = destination.left; left < destination.right; left += horizontal_step) {
      const float drawn_width = std::min(horizontal_step, destination.right - left);
      const float source_drawn_width =
          tile_horizontal || fixed_horizontal
              ? source_width * drawn_width / natural_width
              : source_width;
      const D2D1_RECT_F source_tile =
          D2D1::RectF(source.left, source.top, source.left + source_drawn_width,
                      source.top + source_drawn_height);
      const D2D1_RECT_F destination_tile = D2D1::RectF(
          SnapBitmapPatchCoordinate(render_target, left, true),
          SnapBitmapPatchCoordinate(render_target, top, false),
          SnapBitmapPatchCoordinate(render_target, left + drawn_width, true),
          SnapBitmapPatchCoordinate(render_target, top + drawn_height, false));
      if (destination_tile.right > destination_tile.left &&
          destination_tile.bottom > destination_tile.top) {
        render_target->DrawBitmap(bitmap, destination_tile, 1.0F,
                                  D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, source_tile);
      }
      if (fixed_horizontal) break;
    }
    if (fixed_vertical) break;
  }
  render_target->SetAntialiasMode(previous_antialias_mode);
}

}  // namespace ziliu::ui::detail
