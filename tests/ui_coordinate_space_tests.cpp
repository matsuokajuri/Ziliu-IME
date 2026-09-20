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
  using ziliu::ui::detail::CandidatePixelsToCoordinate;
  using ziliu::ui::detail::CandidateRenderDpi;
  using ziliu::ui::detail::ResolveCandidateCoordinateScale;

  for (const float system_scale : {1.0F, 1.25F, 1.5F, 2.0F}) {
    const float custom_following =
        ResolveCandidateCoordinateScale(true, true, system_scale);
    const float custom_fixed =
        ResolveCandidateCoordinateScale(true, false, system_scale);
    const float native_scale =
        ResolveCandidateCoordinateScale(false, false, system_scale);
    ExpectNear(custom_following, system_scale,
               "custom themes must follow monitor DPI by default");
    ExpectNear(custom_fixed, 1.0F,
               "custom themes must stay at 96 DPI when scaling is disabled");
    ExpectNear(native_scale, system_scale,
               "native and custom-theme fallback rendering must ignore the custom flag");
  }

  constexpr float kFixedScale =
      ResolveCandidateCoordinateScale(true, false, 1.5F);
  ExpectNear(CandidateRenderDpi(kFixedScale), 96.0F,
             "fixed custom rendering must use a 96-DPI target");
  Expect(CandidateCoordinateToPixels(625.0F, kFixedScale) == 625,
         "fixed custom geometry must remain 625 physical pixels wide");
  ExpectNear(CandidatePixelsToCoordinate(1280, kFixedScale), 1280.0F,
             "fixed custom hit testing must use the same physical coordinate space");

  constexpr float kFollowingScale =
      ResolveCandidateCoordinateScale(true, true, 1.5F);
  ExpectNear(CandidateRenderDpi(kFollowingScale), 144.0F,
             "default custom rendering must continue using monitor DPI");
  Expect(CandidateCoordinateToPixels(250.0F, kFollowingScale) == 375,
         "default custom geometry must continue scaling at 150% DPI");
  ExpectNear(CandidatePixelsToCoordinate(1280, kFollowingScale),
             1280.0F / 1.5F,
             "default custom hit testing must continue converting pixels to DIPs");

  ExpectNear(ResolveCandidateCoordinateScale(false, false, 0.0F), 1.0F,
             "an invalid native scale must fail safe to 96 DPI");
  return EXIT_SUCCESS;
}
