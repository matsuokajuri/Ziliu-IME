#include "sogou_background.h"

#include <array>
#include <limits>
#include <new>
#include <set>
#include <utility>

namespace ziliu::ui::detail {
namespace {

SogouBackgroundResult Failure(SogouBackgroundError error,
                              BitmapDecodeError decode_error = BitmapDecodeError::kNone) {
  return {{}, error, decode_error};
}

struct AxisSlices {
  std::array<std::uint32_t, 4> source{};
  std::array<std::uint32_t, 4> target{};
  SogouPatchLayout middle = SogouPatchLayout::kFixed;
  bool unsplit = false;
};

bool BuildAxisSlices(std::uint32_t source_length, std::uint32_t target_length,
                      std::uint32_t leading, std::uint32_t trailing,
                      core::ThemeImageLayout layout, OverlappingBorderPolicy policy,
                      AxisSlices* slices) {
  switch (layout) {
    case core::ThemeImageLayout::kStretch: slices->middle = SogouPatchLayout::kStretch; break;
    case core::ThemeImageLayout::kTile: slices->middle = SogouPatchLayout::kTile; break;
    case core::ThemeImageLayout::kFixed: slices->middle = SogouPatchLayout::kFixed; break;
    default: return false;
  }
  const std::uint64_t caps = static_cast<std::uint64_t>(leading) + trailing;
  if (layout == core::ThemeImageLayout::kFixed && source_length != target_length) {
    return false;
  }
  if (caps >= source_length && policy == OverlappingBorderPolicy::kWholeAxis) {
    slices->source = {0U, 0U, source_length, source_length};
    slices->target = {0U, 0U, target_length, target_length};
    slices->unsplit = true;
    return true;
  }
  if (caps > source_length || caps > target_length ||
      (caps == source_length && caps < target_length)) {
    return false;
  }
  if (layout == core::ThemeImageLayout::kFixed) {
    slices->source = {0U, 0U, source_length, source_length};
    slices->target = slices->source;
    return true;
  }
  slices->source = {0U, leading, source_length - trailing, source_length};
  slices->target = {0U, leading, target_length - trailing, target_length};
  return true;
}

SogouBackgroundResult Render(const core::SogouThemePackageConversion& package,
                             const core::SogouThemeResourceBinding& resources,
                             std::uint32_t width, std::uint32_t height,
                             BitmapSurfaceScale scale, OverlappingBorderPolicy policy,
                             const BitmapDecodeLimits& limits) {
  const auto& conversion = package.conversion;
  const auto& manifest = conversion.manifest;
  if (!package.ok() || !core::ValidateThemeManifest(manifest).empty()) {
    return Failure(SogouBackgroundError::kInvalidTheme);
  }
  if (!resources.ok() || resources.source_package_sha256 != manifest.source_package_sha256 ||
      resources.assets.size() != conversion.assets.size()) {
    return Failure(SogouBackgroundError::kInvalidBinding);
  }
  if (!manifest.appearance.horizontal.has_value() ||
      !manifest.appearance.horizontal->background.has_value()) {
    return Failure(SogouBackgroundError::kMissingH1Background);
  }
  const core::ThemeImage& image = *manifest.appearance.horizontal->background;
  if (!image.horizontal_layout.has_value() || !image.vertical_layout.has_value()) {
    return Failure(SogouBackgroundError::kMissingLayout);
  }
  if (width == 0U || height == 0U ||
      (scale != BitmapSurfaceScale::kUnscaled && scale != BitmapSurfaceScale::kMitchell2x) ||
      (policy != OverlappingBorderPolicy::kReject && policy != OverlappingBorderPolicy::kWholeAxis)) {
    return Failure(SogouBackgroundError::kInvalidGeometry);
  }
  const std::uint64_t output_count = static_cast<std::uint64_t>(width) * height;
  if (width > limits.maximum_dimension || height > limits.maximum_dimension ||
      output_count > limits.maximum_pixels ||
      width > (std::numeric_limits<std::uint32_t>::max)() / 4U ||
      output_count > (std::numeric_limits<std::size_t>::max)() / sizeof(SogouPbgra8)) {
    return Failure(SogouBackgroundError::kResourceLimit);
  }
  const core::SogouThemeResolvedAsset* selected = nullptr;
  std::set<std::string> targets;
  for (std::size_t index = 0; index < resources.assets.size(); ++index) {
    const auto& actual = resources.assets[index];
    const auto& expected = conversion.assets[index];
    if (actual.source_path != expected.source_path || actual.target_path != expected.target_path ||
        !targets.insert(actual.target_path).second) {
      return Failure(SogouBackgroundError::kInvalidBinding);
    }
    if (actual.target_path == image.asset) {
      selected = &actual;
    }
  }
  if (selected == nullptr) {
    return Failure(SogouBackgroundError::kInvalidBinding);
  }
  const auto decoded = DecodePngBitmap(selected->bytes, limits);
  if (!decoded.ok()) {
    return Failure(SogouBackgroundError::kDecodeFailed, decoded.error);
  }
  SogouSurfaceBitmap source;
  if (!PrepareSogouBitmapSurface(decoded.bitmap, scale, &source, limits.maximum_pixels)) {
    return Failure(SogouBackgroundError::kSurfacePreparationFailed);
  }
  const std::uint32_t factor = scale == BitmapSurfaceScale::kMitchell2x ? 2U : 1U;
  AxisSlices horizontal;
  AxisSlices vertical;
  if (!BuildAxisSlices(source.width, width, image.stretch.left * factor,
                        image.stretch.right * factor, *image.horizontal_layout, policy, &horizontal) ||
      !BuildAxisSlices(source.height, height, image.stretch.top * factor,
                        image.stretch.bottom * factor, *image.vertical_layout, policy, &vertical)) {
    return Failure(SogouBackgroundError::kInvalidGeometry);
  }
  SogouH1Background background;
  background.source_package_sha256 = resources.source_package_sha256;
  background.source_path = selected->source_path;
  background.target_path = selected->target_path;
  background.width = width;
  background.height = height;
  background.stride = width * 4U;
  background.unsplit_horizontal = horizontal.unsplit;
  background.unsplit_vertical = vertical.unsplit;
  background.pixels.resize(static_cast<std::size_t>(output_count));
  for (std::size_t y = 0; y < 3U; ++y) {
    for (std::size_t x = 0; x < 3U; ++x) {
      const SogouPixelRect target_rect{horizontal.target[x], vertical.target[y],
                                       horizontal.target[x + 1U], vertical.target[y + 1U]};
      if (target_rect.left == target_rect.right || target_rect.top == target_rect.bottom) {
        continue;
      }
      const SogouPixelRect source_rect{horizontal.source[x], vertical.source[y],
                                       horizontal.source[x + 1U], vertical.source[y + 1U]};
      if (!CompositeSogouRgbaPatchNearest(
              source.pixels, source.width, source.height, source_rect,
              background.pixels, width, height, target_rect,
              x == 1U ? horizontal.middle : SogouPatchLayout::kFixed,
              y == 1U ? vertical.middle : SogouPatchLayout::kFixed)) {
        return Failure(SogouBackgroundError::kInvalidGeometry);
      }
    }
  }
  return {std::move(background), SogouBackgroundError::kNone, BitmapDecodeError::kNone};
}

}  // namespace

SogouBackgroundResult RenderSogouH1Background(
    const core::SogouThemePackageConversion& package,
    const core::SogouThemeResourceBinding& resources,
    std::uint32_t output_width, std::uint32_t output_height,
    BitmapSurfaceScale source_scale, OverlappingBorderPolicy border_policy,
    const BitmapDecodeLimits& limits) {
  try {
    return Render(package, resources, output_width, output_height, source_scale, border_policy, limits);
  } catch (const std::bad_alloc&) {
    return Failure(SogouBackgroundError::kOutOfMemory);
  }
}

}  // namespace ziliu::ui::detail
