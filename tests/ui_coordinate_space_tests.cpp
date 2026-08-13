#include "../src/ui/src/candidate_coordinate_space.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {

void Expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

void ExpectNear(float actual, float expected, std::string_view message) {
  Expect(std::abs(actual - expected) < 0.001F, message);
}

}  // namespace

int main() {
  using ziliu::ui::detail::CandidateCoordinateToPixels;
  using ziliu::ui::detail::CandidatePixelPoint;
  using ziliu::ui::detail::CandidatePixelsToCoordinate;
  using ziliu::ui::detail::CandidateRenderDpi;
  using ziliu::ui::detail::CandidateThemeOffsetToPixels;
  using ziliu::ui::detail::ClampCandidateWindowOrigin;
  using ziliu::ui::detail::ResolveCandidateLayoutScale;
  using ziliu::ui::detail::ResolveCandidateTargetDpi;
  using ziliu::ui::detail::ResolveThemeBitmapLogicalSize;
  using ziliu::ui::detail::ResolveThemeUnitScale;

  Expect(ResolveCandidateTargetDpi(144, 96) == 144,
         "monitor DPI should take precedence over stale window DPI");
  Expect(ResolveCandidateTargetDpi(0, 120) == 120,
         "window DPI should be used when monitor DPI is unavailable");
  Expect(ResolveCandidateTargetDpi(0, 0) == 96,
         "invalid DPI inputs should fall back to 96 DPI");

  ExpectNear(CandidateRenderDpi(1.5F), 144.0F,
             "render DPI should follow coordinate scale");
  ExpectNear(CandidateRenderDpi(0.0F), 96.0F,
             "invalid coordinate scale should fall back to 96 DPI");
  Expect(CandidateCoordinateToPixels(250.0F, 1.5F) == 375,
         "logical coordinates should convert to physical pixels");
  ExpectNear(CandidatePixelsToCoordinate(1280, 1.5F), 1280.0F / 1.5F,
             "physical pixels should convert back to coordinates");

  ExpectNear(ResolveThemeUnitScale(192.0F, 1.0F), 0.5F,
             "theme units should normalize through authoring DPI");
  ExpectNear(ResolveThemeUnitScale(96.0F, 1.25F), 1.25F,
             "explicit layout scale should affect theme units");
  constexpr auto kBase96Bitmap =
      ResolveThemeBitmapLogicalSize(640U, 320U, 96.0F, 1.0F);
  ExpectNear(kBase96Bitmap.width, 640.0F,
             "base-96 bitmap width should retain source units");
  ExpectNear(kBase96Bitmap.height, 320.0F,
             "base-96 bitmap height should retain source units");
  constexpr auto kBase192Bitmap =
      ResolveThemeBitmapLogicalSize(640U, 320U, 192.0F, 1.0F);
  ExpectNear(kBase192Bitmap.width, 320.0F,
             "base-192 bitmap width should normalize to 96-DPI units");
  Expect(CandidateThemeOffsetToPixels(20, 0.5F, 2.0F) == 20,
         "theme offsets should combine theme and coordinate scales once");
  Expect(CandidateThemeOffsetToPixels(-5, 1.0F, 1.5F) == -8,
         "negative theme offsets should use symmetric rounding");

  Expect(ClampCandidateWindowOrigin({100, 100}, 400, 200, 90, 0, 0,
                                    1920, 1040) ==
             CandidatePixelPoint{100, 100},
         "an in-bounds origin should be preserved");
  Expect(ClampCandidateWindowOrigin({1800, 100}, 400, 200, 90, 0, 0,
                                    1920, 1040) ==
             CandidatePixelPoint{1520, 100},
         "a window crossing the right edge should stay in the work area");
  Expect(ClampCandidateWindowOrigin({100, 1000}, 400, 200, 960, 0, 0,
                                    1920, 1040) ==
             CandidatePixelPoint{100, 758},
         "a window crossing the bottom should move above the input spot");
  Expect(ClampCandidateWindowOrigin({-100, -50}, 400, 200, 20, 0, 0,
                                    1920, 1040) ==
             CandidatePixelPoint{0, 0},
         "a window crossing top-left should clamp to the work area");
  Expect(ClampCandidateWindowOrigin({-100, 100}, 400, 200, 90, -1920, 0,
                                    0, 1040) ==
             CandidatePixelPoint{-400, 100},
         "clamping should preserve negative-coordinate monitors");

  ExpectNear(ResolveCandidateLayoutScale(24.0F, 24.0F, true, false), 1.0F,
             "no explicit font override should preserve layout scale");
  ExpectNear(ResolveCandidateLayoutScale(24.0F, 20.0F, true, true),
             20.0F / 24.0F,
             "explicit font override should scale theme geometry");
  ExpectNear(ResolveCandidateLayoutScale(24.0F, 20.0F, false, true), 1.0F,
             "disabled text scaling should preserve theme geometry");
  ExpectNear(ResolveCandidateLayoutScale(1.0F, 10.0F, true, true), 2.0F,
             "layout scale should clamp to its upper safety bound");

  return EXIT_SUCCESS;
}
