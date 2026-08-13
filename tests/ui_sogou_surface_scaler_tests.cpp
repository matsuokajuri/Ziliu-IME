#include "../src/ui/src/sogou_surface_scaler.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

void Expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}

void TestMitchellWeights() {
  using ziliu::ui::detail::SogouMitchellNetravaliWeight;
  constexpr double kTolerance = 1.0e-12;
  Expect(std::abs(SogouMitchellNetravaliWeight(0.0) - 8.0 / 9.0) < kTolerance,
         "Mitchell B=C=1/3 centre weight should be 8/9");
  Expect(std::abs(SogouMitchellNetravaliWeight(0.5) - 77.0 / 144.0) < kTolerance,
         "Mitchell half-pixel weight should use the balanced cubic kernel");
  Expect(std::abs(SogouMitchellNetravaliWeight(1.0) - 1.0 / 18.0) < kTolerance,
         "Mitchell first outer tap should retain its positive lobe");
  Expect(std::abs(SogouMitchellNetravaliWeight(1.5) + 5.0 / 144.0) < kTolerance,
         "Mitchell second outer tap should retain its negative lobe");
  Expect(SogouMitchellNetravaliWeight(2.0) == 0.0,
         "Mitchell support must stop at two source pixels");
}

void TestEdgeContributorNormalization() {
  using ziliu::ui::detail::BuildSogouMitchell2xContributors;

  const auto left_edge = BuildSogouMitchell2xContributors(0, 2);
  Expect(left_edge.first == 0 && left_edge.count == 2,
         "edge contributors must crop invalid taps instead of clamping them");
  double total = 0.0;
  for (std::uint32_t index = 0; index < left_edge.count; ++index) {
    total += left_edge.weights[index];
  }
  Expect(std::abs(total - 1.0) < 1.0e-15,
         "cropped edge contributors must be normalized per output pixel");

  const auto interior = BuildSogouMitchell2xContributors(10, 324);
  Expect(interior.count <= interior.weights.size(),
         "interior contributor construction must never exceed its fixed table");
  Expect(interior.first == 3 && interior.count == 4,
         "the six-coordinate interior interval must discard its low zero tap "
         "before storage and trim its trailing zero tap");
  total = 0.0;
  for (std::uint32_t index = 0; index < interior.count; ++index) {
    Expect(std::isfinite(interior.weights[index]),
           "interior contributor weights must remain finite");
    total += interior.weights[index];
  }
  Expect(std::abs(total - 1.0) < 1.0e-15,
         "interior contributor weights must remain normalized");
}

void TestPremultipliedTwoPassQuantization() {
  using ziliu::ui::detail::ScaleSogouRgbaMitchell2x;
  using ziliu::ui::detail::SogouPbgra8;
  using ziliu::ui::detail::SogouRgba8;
  const std::array source{
      SogouRgba8{255, 0, 0, 255},
      SogouRgba8{0, 255, 0, 128},
      SogouRgba8{0, 0, 255, 64},
      SogouRgba8{255, 255, 255, 0},
  };
  const std::vector<SogouPbgra8> expected{
      {68, 0, 0, 68},    {49, 0, 0, 49},     {16, 0, 0, 16},   {0, 0, 0, 0},
      {50, 0, 63, 113},  {36, 8, 47, 91},    {12, 24, 16, 51}, {0, 33, 0, 33},
      {16, 0, 192, 208}, {12, 24, 145, 181}, {4, 72, 47, 124}, {0, 99, 0, 99},
      {0, 0, 255, 255},  {0, 33, 198, 229},  {0, 99, 65, 163}, {0, 136, 0, 136},
  };
  std::vector<SogouPbgra8> actual;
  Expect(ScaleSogouRgbaMitchell2x(source, 2, 2, &actual),
         "2x Mitchell scaling should accept a complete RGBA source");
  Expect(actual == expected, "2x Mitchell scaling must preserve bottom-up DIB phase and quantize "
                             "premultiplied BGRA after both passes");
}

void TestTransparentEdgeAndPbgraStorage() {
  using ziliu::ui::detail::QuantizeSogouMitchellPass;
  using ziliu::ui::detail::ScaleSogouRgbaMitchell2x;
  using ziliu::ui::detail::SogouPbgra8;
  using ziliu::ui::detail::SogouRgba8;

  const std::array transparent_source{SogouRgba8{255, 200, 100, 0}};
  std::vector<SogouPbgra8> transparent_scaled;
  Expect(ScaleSogouRgbaMitchell2x(transparent_source, 1, 1, &transparent_scaled),
         "a transparent edge pixel should scale successfully");
  Expect(transparent_scaled == std::vector<SogouPbgra8>(4, SogouPbgra8{0, 0, 0, 0}),
         "premultiplied scaling must not leak hidden RGB at a transparent edge");

  const std::array translucent_source{SogouRgba8{200, 100, 50, 128}};
  std::vector<SogouPbgra8> translucent_scaled;
  Expect(ScaleSogouRgbaMitchell2x(translucent_source, 1, 1, &translucent_scaled),
         "a translucent edge pixel should scale successfully");
  Expect(translucent_scaled == std::vector<SogouPbgra8>(4, SogouPbgra8{25, 50, 100, 128}),
         "source premultiplication must floor before the two Mitchell passes");
  static_assert(sizeof(SogouPbgra8) == 4U);
  Expect(translucent_scaled.front() == SogouPbgra8{25, 50, 100, 128},
         "scaled pixels must already be upload-ready PBGRA values");
  Expect(QuantizeSogouMitchellPass({200.0, 20.0, 30.0, 100.0}) == SogouPbgra8{200, 20, 30, 200},
         "each scaler pass must raise alpha to its largest premultiplied channel");
}

void TestScaledTileInsetAndNearestComposition() {
  using ziliu::ui::detail::CompositeSogouRgbaPatchNearest;
  using ziliu::ui::detail::SogouPatchLayout;
  using ziliu::ui::detail::SogouPbgra8;
  using ziliu::ui::detail::SogouPixelRect;

  std::vector<SogouPbgra8> source(3);
  for (std::size_t index = 0; index < source.size(); ++index) {
    source[index] = {static_cast<std::uint8_t>(index % 251U), 0, 0, 255};
  }
  std::vector<SogouPbgra8> target(6);
  Expect(CompositeSogouRgbaPatchNearest(
             source, 3, 1, SogouPixelRect{0, 0, 3, 1}, target, 6, 1,
             SogouPixelRect{0, 0, 6, 1}, SogouPatchLayout::kTile,
             SogouPatchLayout::kStretch),
         "nearest compositor should accept a complete tile");
  Expect(target[0] == source[0] && target[2] == source[2] &&
             target[3] == source[0] && target[5] == source[2],
         "tile composition should repeat the complete source patch");

  const std::array vertical_source{SogouPbgra8{0, 0, 2, 255}, SogouPbgra8{0, 0, 1, 255}};
  std::array<SogouPbgra8, 4> vertical_target{};
  Expect(CompositeSogouRgbaPatchNearest(vertical_source, 1, 2, SogouPixelRect{0, 0, 1, 2},
                                        vertical_target, 1, 4, SogouPixelRect{0, 0, 1, 4},
                                        SogouPatchLayout::kStretch,
                                        SogouPatchLayout::kStretch),
         "nearest compositor should stretch a two-row patch");
  Expect(vertical_target[0].red == 1 && vertical_target[1].red == 1 &&
             vertical_target[2].red == 2 && vertical_target[3].red == 2,
         "nearest stretch should use destination pixel centres");
}

} // namespace

int main() {
  TestMitchellWeights();
  TestEdgeContributorNormalization();
  TestPremultipliedTwoPassQuantization();
  TestTransparentEdgeAndPbgraStorage();
  TestScaledTileInsetAndNearestComposition();
  return 0;
}
