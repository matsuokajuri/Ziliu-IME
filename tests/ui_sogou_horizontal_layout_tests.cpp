#include "../src/ui/src/sogou_horizontal_layout.h"

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

void ExpectBounds(
    const ziliu::ui::detail::SogouHorizontalBounds& actual, float left,
    float top, float right, float bottom, std::string_view message) {
  ExpectNear(actual.left, left, message);
  ExpectNear(actual.top, top, message);
  ExpectNear(actual.right, right, message);
  ExpectNear(actual.bottom, bottom, message);
}

}  // namespace

int main() {
  using ziliu::ui::detail::BuildSogouHorizontalCandidateLabel;
  using ziliu::ui::detail::BuildSogouVerticalCandidateLabel;
  using ziliu::ui::detail::ResolveSogouHorizontalActionLayout;
  using ziliu::ui::detail::ResolveSogouHorizontalCandidateTextTop;
  using ziliu::ui::detail::ResolveSogouHorizontalCandidateWidth;
  using ziliu::ui::detail::ResolveSogouHorizontalPreeditCaretBounds;
  using ziliu::ui::detail::ResolveSogouHorizontalPreeditTextTop;
  using ziliu::ui::detail::ResolveSogouVerticalCandidateRowHeight;
  using ziliu::ui::detail::ResolveSogouVerticalCandidateTextTop;
  using ziliu::ui::detail::ResolveSogouVerticalPreeditCaretBounds;
  using ziliu::ui::detail::ResolveSogouVerticalPreeditHeight;
  using ziliu::ui::detail::ResolveSogouVerticalPreeditTextTop;
  using ziliu::ui::detail::ResolveSogouVerticalPagerLayout;
  using ziliu::ui::detail::ResolveSogouVerticalSurfaceHeight;
  using ziliu::ui::detail::ResolveSogouVerticalSurfaceWidth;
  using ziliu::ui::detail::ResolveSogouPreeditCaretDWriteFontSize;
  using ziliu::ui::detail::ResolveSogouPreeditDWriteFontSize;
  using ziliu::ui::detail::SogouHorizontalActionStripWidth;
  using ziliu::ui::detail::UseNearestNeighborForThemeBitmap;

  Expect(BuildSogouHorizontalCandidateLabel(1, L"你好") == L"1.你好",
         "Sogou H1 labels must use a period without spaces");
  Expect(BuildSogouHorizontalCandidateLabel(10, L"候选") == L"10.候选",
         "multi-digit selection labels must preserve the same contract");
  Expect(BuildSogouVerticalCandidateLabel(1, L"你好") == L"1.你好",
         "Sogou V1 labels must use the same period without spaces");

  ExpectNear(ResolveSogouHorizontalCandidateWidth(48.0F), 63.0F,
             "a two-Han candidate should add the measured 15px gap");
  ExpectNear(ResolveSogouHorizontalCandidateWidth(32.0F), 57.0F,
             "short candidates should retain the measured 57px minimum");
  ExpectNear(ResolveSogouHorizontalCandidateWidth(66.0F), 81.0F,
             "long candidates should remain content-sized");
  ExpectNear(ResolveSogouHorizontalCandidateWidth(48.0F, 1.25F), 71.25F,
             "candidate minimum width must scale with the target surface");
  ExpectNear(SogouHorizontalActionStripWidth(), 90.0F,
             "the fallback action strip should reserve exactly 90px");
  ExpectNear(ResolveSogouPreeditDWriteFontSize(17.0F), 16.0F,
             "the SSF preedit raster should use the calibrated 16px em size");
  ExpectNear(ResolveSogouPreeditCaretDWriteFontSize(17.0F), 15.0F,
             "the SSF caret must retain its separately calibrated advance");
  ExpectNear(ResolveSogouHorizontalCandidateTextTop(137.0F), 135.0F,
             "candidate mixed text should move two pixels upward");
  ExpectNear(ResolveSogouHorizontalPreeditTextTop(116.0F), 118.0F,
             "preedit glyphs should start two pixels below the SSF origin");
  ExpectNear(ResolveSogouVerticalPreeditHeight(45.0F, 17.0F, 10.0F),
             76.0F,
             "the real V1 preedit allocation includes four trailing pixels");
  ExpectNear(ResolveSogouVerticalCandidateRowHeight(17.0F), 27.0F,
             "the real V1 candidate baselines advance by 27 pixels");
  ExpectNear(ResolveSogouVerticalCandidateTextTop(86.0F), 86.0F,
             "vertical candidate glyphs retain the measured row baseline");
  ExpectNear(ResolveSogouVerticalPreeditTextTop(45.0F), 47.0F,
             "vertical preedit glyphs use the calibrated two-pixel drop");
  ExpectNear(
      ResolveSogouVerticalSurfaceWidth(210.0F, 24.0F, 89.0F, 175.0F),
      288.0F,
      "the right-anchored overlay must reserve its full width after candidate text");
  ExpectNear(ResolveSogouVerticalSurfaceHeight(305.0F, true), 319.0F,
             "the fallback pager footer must produce the measured 319px surface");
  ExpectNear(
      ResolveSogouVerticalSurfaceWidth(210.0F, 24.0F, 42.0F, 0.0F),
      210.0F,
      "Sogou surfaces without a right overlay must retain their content width");
  ExpectNear(ResolveSogouVerticalSurfaceHeight(305.0F, false), 305.0F,
             "surfaces with image buttons must not reserve the fallback footer");
  const auto caret = ResolveSogouHorizontalPreeditCaretBounds(
      250.0F, 39.6F, 116.0F, 133.0F);
  ExpectBounds(caret, 292.0F, 118.0F, 293.0F, 136.0F,
               "the preedit caret should match the measured Sogou pixels");
  const auto vertical_caret = ResolveSogouVerticalPreeditCaretBounds(
      24.0F, 39.6F, 45.0F, 17.0F);
  ExpectBounds(vertical_caret, 66.0F, 47.0F, 67.0F, 65.0F,
               "the vertical caret should match the measured Sogou pixels");
  const auto vertical_pager =
      ResolveSogouVerticalPagerLayout(24.0F, 275.0F);
  ExpectBounds(vertical_pager.previous_icon, 24.0F, 278.0F, 30.0F,
               289.0F, "the disabled previous glyph must match the target");
  ExpectBounds(vertical_pager.next_icon, 36.0F, 278.0F, 42.0F, 289.0F,
               "the active next glyph must match the target");

  const auto layout = ResolveSogouHorizontalActionLayout(
      762.0F, 100.0F, 137.0F, 36.0F);
  ExpectBounds(layout.strip, 572.0F, 137.0F, 662.0F, 173.0F,
               "the 636332 action strip should match the reference crop");
  ExpectBounds(layout.previous_hit_target, 572.0F, 137.0F, 602.0F,
               173.0F, "the first-page previous slot stays transparent");
  ExpectBounds(layout.next_expand_hit_target, 602.0F, 137.0F, 635.0F,
               173.0F, "the next/expand hit target should precede separator");
  ExpectBounds(layout.separator, 635.0F, 144.0F, 636.0F, 164.0F,
               "the separator should match the target pixel bounds");
  ExpectBounds(layout.menu_hit_target, 636.0F, 137.0F, 662.0F, 173.0F,
               "the hamburger hit target should fill the last action slot");
  ExpectBounds(layout.next_expand_icon, 614.0F, 146.0F, 629.0F,
               161.0F, "the next/expand icon should be 15px square");
  ExpectBounds(layout.menu_line_top, 644.0F, 148.0F, 660.0F, 150.0F,
               "the top hamburger stroke should match the target crop");
  ExpectBounds(layout.menu_line_middle, 644.0F, 153.0F, 660.0F,
               155.0F, "the middle hamburger stroke should match target");
  ExpectBounds(layout.menu_line_bottom, 644.0F, 158.0F, 660.0F,
               160.0F, "the bottom hamburger stroke should match target");

  Expect(UseNearestNeighborForThemeBitmap(true),
         "imported Sogou bitmaps must use nearest-neighbor sampling");
  Expect(!UseNearestNeighborForThemeBitmap(false),
         "native themes must preserve their existing linear sampling");
  return EXIT_SUCCESS;
}
