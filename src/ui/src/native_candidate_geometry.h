#pragma once

#include <d2d1.h>
#include <d2d1helper.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string_view>

namespace ziliu::ui::detail {

struct NativeTextLine {
  Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
  D2D1_POINT_2F origin_offset{};
  float width = 0.0F;
  float height = 0.0F;
};

// Font size is an em size, not the height of a rendered line. Include fallback
// line metrics, ink overhang, and one physical pixel for antialias coverage.
inline NativeTextLine MeasureNativeTextLine(IDWriteFactory* factory,
                                           IDWriteTextFormat* format,
                                           std::wstring_view text,
                                           float maximum_width,
                                           float dpi_scale) {
  NativeTextLine result;
  if (factory == nullptr || format == nullptr || text.empty() ||
      text.size() > std::numeric_limits<UINT32>::max()) {
    return result;
  }
  if (FAILED(factory->CreateTextLayout(
          text.data(), static_cast<UINT32>(text.size()), format,
          std::max(maximum_width, 1.0F), format->GetFontSize() * 4.0F,
          result.layout.GetAddressOf())) ||
      FAILED(result.layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP)) ||
      FAILED(result.layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR))) {
    return {};
  }
  DWRITE_TEXT_METRICS metrics{};
  if (FAILED(result.layout->GetMetrics(&metrics)) ||
      FAILED(result.layout->SetMaxWidth(std::max(metrics.widthIncludingTrailingWhitespace, 1.0F))) ||
      FAILED(result.layout->SetMaxHeight(std::max(metrics.height, 1.0F)))) {
    return {};
  }
  DWRITE_OVERHANG_METRICS overhang{};
  if (FAILED(result.layout->GetOverhangMetrics(&overhang))) {
    return {};
  }
  const float scale = std::max(dpi_scale, 1.0F);
  const auto ink_padding = [scale](float value) {
    return (std::ceil(std::max(value, 0.0F) * scale) + 1.0F) / scale;
  };
  result.origin_offset = D2D1::Point2F(ink_padding(overhang.left), ink_padding(overhang.top));
  result.width = std::ceil((metrics.widthIncludingTrailingWhitespace +
                           result.origin_offset.x + ink_padding(overhang.right)) * scale) / scale;
  result.height = std::ceil((metrics.height + result.origin_offset.y +
                            ink_padding(overhang.bottom)) * scale) / scale;
  return result;
}

inline float NativeCandidateRowHeight(float ink_height, float layout_scale) {
  return std::ceil(ink_height + 4.0F * layout_scale);
}

inline float NativeFadeOpacity(float from, float to, float elapsed, float duration) {
  const float t = duration <= 0.0F ? 1.0F : std::clamp(elapsed / duration, 0.0F, 1.0F);
  const float eased = t * t * (3.0F - 2.0F * t);
  return std::clamp(from + (to - from) * eased, 0.0F, 1.0F);
}

// Interpolate physical window edges, never glyphs or their advances. Interpolating
// both edges also keeps a right-edge-constrained popup inside its work area.
inline RECT NativeWidthFrame(const RECT& from, const RECT& to, float elapsed, float duration) {
  const float progress = NativeFadeOpacity(0.0F, 1.0F, elapsed, duration);
  const auto edge = [progress](LONG first, LONG last) {
    return static_cast<LONG>(std::lround(static_cast<double>(first) +
        (static_cast<double>(last) - first) * progress));
  };
  return RECT{edge(from.left, to.left), to.top, edge(from.right, to.right), to.bottom};
}

// Each corner is a cubic Bezier with both controls at the rectangle corner.
// Its tangent follows the adjoining straight edge and its endpoint curvature
// is zero, giving a continuous transition instead of a circular arc join.
inline Microsoft::WRL::ComPtr<ID2D1PathGeometry> CreateNativeRoundedGeometry(
    ID2D1Factory* factory, const D2D1_RECT_F& bounds, float radius) {
  Microsoft::WRL::ComPtr<ID2D1PathGeometry> geometry;
  if (factory == nullptr || bounds.right <= bounds.left || bounds.bottom <= bounds.top ||
      FAILED(factory->CreatePathGeometry(geometry.GetAddressOf()))) {
    return {};
  }
  Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
  if (FAILED(geometry->Open(sink.GetAddressOf()))) {
    return {};
  }
  const float r = std::clamp(radius, 0.0F,
                            std::min(bounds.right - bounds.left, bounds.bottom - bounds.top) * 0.5F);
  const auto point = [](float x, float y) { return D2D1::Point2F(x, y); };
  sink->BeginFigure(point(bounds.left + r, bounds.top), D2D1_FIGURE_BEGIN_FILLED);
  sink->AddLine(point(bounds.right - r, bounds.top));
  sink->AddBezier(D2D1::BezierSegment(point(bounds.right, bounds.top),
                                     point(bounds.right, bounds.top), point(bounds.right, bounds.top + r)));
  sink->AddLine(point(bounds.right, bounds.bottom - r));
  sink->AddBezier(D2D1::BezierSegment(point(bounds.right, bounds.bottom),
                                     point(bounds.right, bounds.bottom), point(bounds.right - r, bounds.bottom)));
  sink->AddLine(point(bounds.left + r, bounds.bottom));
  sink->AddBezier(D2D1::BezierSegment(point(bounds.left, bounds.bottom),
                                     point(bounds.left, bounds.bottom), point(bounds.left, bounds.bottom - r)));
  sink->AddLine(point(bounds.left, bounds.top + r));
  sink->AddBezier(D2D1::BezierSegment(point(bounds.left, bounds.top),
                                     point(bounds.left, bounds.top), point(bounds.left + r, bounds.top)));
  sink->EndFigure(D2D1_FIGURE_END_CLOSED);
  if (FAILED(sink->Close())) {
    return {};
  }
  return geometry;
}

// Nested cubic silhouettes approximate a Gaussian falloff without a second
// HWND or a new graphics device. The opaque surface covers the inner shadow.
inline void DrawNativeCandidateShadow(ID2D1Factory* factory, ID2D1RenderTarget* target,
                                      const D2D1_RECT_F& surface, float radius,
                                      float scale, bool dark) {
  Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
  if (FAILED(target->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Black),
                                          brush.GetAddressOf()))) {
    return;
  }
  const float strength = dark ? 0.30F : 0.20F;
  const auto alpha = [strength](float distance) {
    return strength * std::exp(-distance * distance / 24.5F);
  };
  float accumulated = 0.0F;
  for (int ring = 9; ring >= 0; --ring) {
    const float opacity = alpha(static_cast<float>(ring));
    brush->SetOpacity((opacity - accumulated) / (1.0F - accumulated));
    accumulated = opacity;
    const float spread = static_cast<float>(ring) * scale;
    const auto bounds = D2D1::RectF(surface.left - spread, surface.top - spread + 2.0F * scale,
                                    surface.right + spread, surface.bottom + spread + 2.0F * scale);
    const auto geometry = CreateNativeRoundedGeometry(factory, bounds, radius + spread);
    if (geometry != nullptr) {
      target->FillGeometry(geometry.Get(), brush.Get());
    }
  }
}

}  // namespace ziliu::ui::detail
