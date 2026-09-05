#pragma once

#include "ziliu/core/sogou_theme.h"
#include "sogou_bitmap_surface.h"

namespace ziliu::ui::detail {

// Neither policy is implicitly selected from a skin ID or screenshot.
enum class OverlappingBorderPolicy { kReject, kWholeAxis };

enum class SogouBackgroundError {
  kNone, kInvalidTheme, kInvalidBinding, kMissingH1Background, kMissingLayout,
  kInvalidGeometry, kResourceLimit, kDecodeFailed, kSurfacePreparationFailed, kOutOfMemory,
};

struct SogouH1Background {
  std::string source_package_sha256;
  std::string source_path;
  std::string target_path;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint32_t stride = 0;
  // True only when the explicitly requested whole-axis policy was needed.
  bool unsplit_horizontal = false;
  bool unsplit_vertical = false;
  // Owned, tightly packed PBGRA8 in top-down order. Background layer only.
  std::vector<SogouPbgra8> pixels;
};

struct SogouBackgroundResult {
  SogouH1Background background;
  SogouBackgroundError error = SogouBackgroundError::kNone;
  BitmapDecodeError decode_error = BitmapDecodeError::kNone;
  [[nodiscard]] bool ok() const noexcept { return error == SogouBackgroundError::kNone; }
};

// Explicit physical output size, source scale and overlapping-border policy.
// Stretch uses the existing nearest pixel-centre sampler; tile repeats the
// centre patch. Fixed axes require source-sized output. kWholeAxis uses one
// source patch on an axis whose cap sum reaches/exceeds its image extent.
// Missing layouts and output smaller than valid fixed caps remain errors.
// These are generic capabilities, not verified Sogou pixel-parity policies.
// Does not render text, overlays, pager or choose window geometry/DPI behavior.
[[nodiscard]] SogouBackgroundResult RenderSogouH1Background(
    const core::SogouThemePackageConversion& package,
    const core::SogouThemeResourceBinding& resources,
    std::uint32_t output_width, std::uint32_t output_height,
    BitmapSurfaceScale source_scale, OverlappingBorderPolicy border_policy,
    const BitmapDecodeLimits& limits = {});

}  // namespace ziliu::ui::detail
