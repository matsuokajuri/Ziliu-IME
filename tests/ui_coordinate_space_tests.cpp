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

  constexpr float kSystemScale150Percent = 1.5F;
  constexpr float kSogouScale =
      ResolveCandidateCoordinateScale(true, kSystemScale150Percent);
  constexpr float kNativeScale =
      ResolveCandidateCoordinateScale(false, kSystemScale150Percent);

  ExpectNear(kSogouScale, 1.0F,
             "Sogou SSF coordinates must remain physical pixels at 150% DPI");
  ExpectNear(CandidateRenderDpi(kSogouScale), 96.0F,
             "Sogou SSF Direct2D rendering must stay at 96 DPI");
  Expect(CandidateCoordinateToPixels(625.0F, kSogouScale) == 625,
         "a 625px SSF background must remain 625 physical pixels wide");
  Expect(CandidateCoordinateToPixels(250.0F, kSogouScale) == 250,
         "a 250px SSF background must remain 250 physical pixels high");
  ExpectNear(CandidatePixelsToCoordinate(1280, kSogouScale), 1280.0F,
             "the full physical work area must remain available to an SSF");

  ExpectNear(kNativeScale, 1.5F,
             "native candidate coordinates must continue following system DPI");
  ExpectNear(CandidateRenderDpi(kNativeScale), 144.0F,
             "native Direct2D rendering must continue using monitor DPI");
  Expect(CandidateCoordinateToPixels(250.0F, kNativeScale) == 375,
         "native 250-DIP geometry must continue scaling at 150% DPI");
  ExpectNear(CandidatePixelsToCoordinate(1280, kNativeScale),
             1280.0F / 1.5F,
             "native layout must continue converting the work area to DIPs");

  ExpectNear(ResolveCandidateCoordinateScale(false, 0.0F), 1.0F,
             "an invalid native scale must fail safe to 96 DPI");
  return EXIT_SUCCESS;
}
