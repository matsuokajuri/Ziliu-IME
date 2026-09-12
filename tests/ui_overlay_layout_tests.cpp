#include "../src/ui/src/sogou_overlay_layout.h"

#include <array>
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

void ExpectBounds(const ziliu::ui::detail::OverlayBounds& actual, float left,
                  float top, float right, float bottom,
                  std::string_view message) {
  ExpectNear(actual.left, left, message);
  ExpectNear(actual.top, top, message);
  ExpectNear(actual.right, right, message);
  ExpectNear(actual.bottom, bottom, message);
}

}  // namespace

int main() {
  using ziliu::ui::detail::ResolveSogouOverlayBounds;
  using ziliu::ui::detail::UsesSogouBottomRightEdgeAlign;

  constexpr std::array<std::int32_t, 10> k636332VerticalAlign{
      0, 0, 0, 0, 0, 2, 0, 2, 6, 0};
  ExpectBounds(
      ResolveSogouOverlayBounds(k636332VerticalAlign, 210.0F, 230.0F,
                                175.0F, 191.0F, 1.0F),
      35.0F, 39.0F, 210.0F, 230.0F,
      "636332 V1 should anchor flush to the right and bottom edges");

  ExpectBounds(
      ResolveSogouOverlayBounds(k636332VerticalAlign, 288.0F, 319.0F,
                                175.0F, 191.0F, 1.0F),
      113.0F, 128.0F, 288.0F, 319.0F,
      "the V1 anchor should match the real Sogou vertical surface");

  ExpectBounds(
      ResolveSogouOverlayBounds(k636332VerticalAlign, 262.5F, 287.5F,
                                175.0F, 191.0F, 1.25F),
      43.75F, 48.75F, 262.5F, 287.5F,
      "the artwork should stay flush when the destination scales");
  Expect(UsesSogouBottomRightEdgeAlign(k636332VerticalAlign),
         "the pinned extended tuple should expose its edge-anchor contract");

  constexpr std::array<std::int32_t, 10> kLegacyBounds{
      11, 13, 17, 19, 0, 0, 0, 0, 0, 0};
  ExpectBounds(
      ResolveSogouOverlayBounds(kLegacyBounds, 400.0F, 300.0F, 40.0F,
                                50.0F, 2.0F),
      22.0F, 26.0F, 56.0F, 64.0F,
      "legacy explicit x/y/width/height tuples should remain supported");

  constexpr std::array<std::int32_t, 10> kUnknownExtendedAlign{
      5, 7, 0, 0, 1, 0, 0, 0, 0, 0};
  ExpectBounds(
      ResolveSogouOverlayBounds(kUnknownExtendedAlign, 400.0F, 300.0F,
                                40.0F, 50.0F, 1.0F),
      0.0F, 0.0F, 40.0F, 50.0F,
      "unknown extended tuples should keep the deterministic origin fallback");

  return EXIT_SUCCESS;
}
