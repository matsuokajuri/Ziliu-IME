#include "../src/ui/src/sogou_background.h"

#include <array>
#include <cstdlib>
#include <iostream>
#include <source_location>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
using namespace ziliu::core;
using namespace ziliu::ui;
using namespace ziliu::ui::detail;

// Authored format fixture, not captured reference data.
constexpr std::array<std::uint8_t, 173> kGridPng{
    0x89U, 0x50U, 0x4EU, 0x47U, 0x0DU, 0x0AU, 0x1AU, 0x0AU, 0x00U, 0x00U, 0x00U, 0x0DU,
    0x49U, 0x48U, 0x44U, 0x52U, 0x00U, 0x00U, 0x00U, 0x05U, 0x00U, 0x00U, 0x00U, 0x05U,
    0x08U, 0x06U, 0x00U, 0x00U, 0x00U, 0x8DU, 0x6FU, 0x26U, 0xE5U, 0x00U, 0x00U, 0x00U,
    0x74U, 0x49U, 0x44U, 0x41U, 0x54U, 0x78U, 0x01U, 0x01U, 0x69U, 0x00U, 0x96U, 0xFFU,
    0x00U, 0x00U, 0x00U, 0x32U, 0xFFU, 0x0AU, 0x00U, 0x33U, 0xFFU, 0x14U, 0x00U, 0x34U,
    0xFFU, 0x1EU, 0x00U, 0x35U, 0xFFU, 0x28U, 0x00U, 0x36U, 0xFFU, 0x00U, 0x00U, 0x0AU,
    0x37U, 0xFFU, 0x0AU, 0x0AU, 0x38U, 0xFFU, 0x14U, 0x0AU, 0x39U, 0xFFU, 0x1EU, 0x0AU,
    0x3AU, 0xFFU, 0x28U, 0x0AU, 0x3BU, 0xFFU, 0x00U, 0x00U, 0x14U, 0x3CU, 0xFFU, 0x0AU,
    0x14U, 0x3DU, 0xFFU, 0x14U, 0x14U, 0x3EU, 0xFFU, 0x1EU, 0x14U, 0x3FU, 0xFFU, 0x28U,
    0x14U, 0x40U, 0xFFU, 0x00U, 0x00U, 0x1EU, 0x41U, 0xFFU, 0x0AU, 0x1EU, 0x42U, 0xFFU,
    0x14U, 0x1EU, 0x43U, 0xFFU, 0x1EU, 0x1EU, 0x44U, 0xFFU, 0x28U, 0x1EU, 0x45U, 0xFFU,
    0x00U, 0x00U, 0x28U, 0x46U, 0xFFU, 0x0AU, 0x28U, 0x47U, 0xFFU, 0x14U, 0x28U, 0x48U,
    0xFFU, 0x1EU, 0x28U, 0x49U, 0xFFU, 0x28U, 0x28U, 0x4AU, 0x80U, 0xBBU, 0xFFU, 0x22U,
    0x5FU, 0xF2U, 0x67U, 0xEFU, 0x65U, 0x00U, 0x00U, 0x00U, 0x00U, 0x49U, 0x45U, 0x4EU,
    0x44U, 0xAEU, 0x42U, 0x60U, 0x82U,
};
constexpr std::string_view kPackageIdentity =
    "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

void Expect(bool condition, std::string_view message,
            const std::source_location where = std::source_location::current()) {
  if (!condition) {
    std::cerr << "FAILED at line " << where.line() << ": " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

struct Fixture {
  SogouThemePackageConversion package;
  SogouThemeResourceBinding resources;

  SogouBackgroundResult Render(std::uint32_t width, std::uint32_t height,
                               OverlappingBorderPolicy policy = OverlappingBorderPolicy::kReject,
                               BitmapSurfaceScale scale = BitmapSurfaceScale::kUnscaled,
                               const BitmapDecodeLimits& limits = {}) const {
    return RenderSogouH1Background(package, resources, width, height, scale, policy, limits);
  }
};

Fixture MakeTheme(std::string_view layout) {
  const std::string ini = "[General]\nskin_name=Grid\n[Scheme_H1]\npic=grid.png\n" +
                          std::string(layout);
  const std::vector<std::uint8_t> text(ini.begin(), ini.end());
  const std::array entries{
      SogouThemePackageEntryView{"skin.ini", text},
      SogouThemePackageEntryView{"grid.png", kGridPng}};
  auto package = ConvertSogouThemePackage(entries, "grid.ssf", kPackageIdentity);
  auto resources = ResolveSogouThemePackageResources(package, entries);
  Expect(package.ok() && resources.ok(), "fixture must convert and bind");
  return {std::move(package), std::move(resources)};
}

SogouPbgra8 GridPixel(std::uint32_t x, std::uint32_t y) {
  if (x == 4U && y == 4U) return {37U, 20U, 20U, 128U};
  return {static_cast<std::uint8_t>(50U + y * 5U + x),
          static_cast<std::uint8_t>(y * 10U), static_cast<std::uint8_t>(x * 10U), 255U};
}

void ExpectGrid(const SogouBackgroundResult& result,
                std::span<const std::uint32_t> columns,
                std::span<const std::uint32_t> rows,
                bool unsplit_x = false, bool unsplit_y = false) {
  Expect(result.ok(), "background composition must succeed");
  const auto& bg = result.background;
  Expect(bg.width == columns.size() && bg.height == rows.size() &&
             bg.stride == columns.size() * 4U && bg.pixels.size() == columns.size() * rows.size(),
         "background must have tight requested geometry");
  Expect(bg.source_package_sha256 == kPackageIdentity && bg.source_path == "grid.png" &&
             bg.target_path == "assets/ssf-000.png", "resource identity must survive composition");
  Expect(bg.unsplit_horizontal == unsplit_x && bg.unsplit_vertical == unsplit_y,
         "whole-axis policy use must be reported accurately");
  for (std::size_t y = 0; y < rows.size(); ++y) {
    for (std::size_t x = 0; x < columns.size(); ++x) {
      Expect(bg.pixels[y * columns.size() + x] == GridPixel(columns[x], rows[y]),
             "cap, centre, channel, alpha or row mapping is wrong");
    }
  }
}

void ExpectFailure(const SogouBackgroundResult& result, SogouBackgroundError error,
                   const std::source_location where = std::source_location::current()) {
  if (result.error != error) {
    std::cerr << "at line " << where.line() << " expected=" << static_cast<int>(error)
              << " actual=" << static_cast<int>(result.error) << '\n';
  }
  Expect(!result.ok() && result.error == error, "error category must be explicit");
  Expect(result.background.width == 0U && result.background.height == 0U &&
             result.background.pixels.empty() && result.background.source_package_sha256.empty(),
         "failure must not publish a partial background");
}
}  // namespace

int main() {
  constexpr std::array<std::uint32_t, 5> native{0U, 1U, 2U, 3U, 4U};
  constexpr std::array<std::uint32_t, 9> stretch_x{0U, 1U, 1U, 2U, 2U, 2U, 3U, 3U, 4U};
  constexpr std::array<std::uint32_t, 7> stretch_y{0U, 1U, 1U, 2U, 3U, 3U, 4U};
  constexpr std::array<std::uint32_t, 9> tile_x{0U, 1U, 2U, 3U, 1U, 2U, 3U, 1U, 4U};
  constexpr std::array<std::uint32_t, 7> tile_y{0U, 1U, 2U, 3U, 1U, 2U, 4U};
  constexpr std::array<std::uint32_t, 8> whole_stretch{0U, 0U, 1U, 2U, 2U, 3U, 4U, 4U};
  constexpr std::array<std::uint32_t, 8> whole_tile{0U, 1U, 2U, 3U, 4U, 0U, 1U, 2U};

  const auto stretch = MakeTheme("layout_horizontal=0,1,1\nlayout_vertical=0,1,1\n");
  ExpectGrid(stretch.Render(5U, 5U), native, native);
  ExpectGrid(stretch.Render(9U, 7U), stretch_x, stretch_y);
  const auto tile = MakeTheme("layout_horizontal=1,1,1\nlayout_vertical=1,1,1\n");
  ExpectGrid(tile.Render(9U, 7U), tile_x, tile_y);
  const auto mixed = MakeTheme("layout_horizontal=0,1,1\nlayout_vertical=1,1,1\n");
  ExpectGrid(mixed.Render(9U, 7U), stretch_x, tile_y);
  const auto fixed = MakeTheme("layout_horizontal=0,1,1\nlayout_vertical=2,1,1\n");
  ExpectGrid(fixed.Render(9U, 5U), stretch_x, native);
  ExpectFailure(fixed.Render(9U, 7U), SogouBackgroundError::kInvalidGeometry);
  const auto no_caps = MakeTheme("layout_horizontal=0,0,0\nlayout_vertical=0,0,0\n");
  ExpectGrid(no_caps.Render(5U, 5U), native, native);
  constexpr std::array<std::uint32_t, 2> ends{0U, 4U};
  ExpectGrid(stretch.Render(2U, 2U), ends, ends);

  const auto empty = MakeTheme("layout_horizontal=0,2,3\nlayout_vertical=0,1,1\n");
  ExpectGrid(empty.Render(5U, 5U), native, native);
  ExpectFailure(empty.Render(8U, 5U), SogouBackgroundError::kInvalidGeometry);
  ExpectGrid(empty.Render(8U, 5U, OverlappingBorderPolicy::kWholeAxis),
             whole_stretch, native, true, false);
  const auto overlap_y = MakeTheme("layout_horizontal=1,1,1\nlayout_vertical=0,3,3\n");
  ExpectFailure(overlap_y.Render(9U, 5U), SogouBackgroundError::kInvalidGeometry);
  ExpectGrid(overlap_y.Render(9U, 5U, OverlappingBorderPolicy::kWholeAxis),
             tile_x, native, false, true);
  ExpectGrid(overlap_y.Render(9U, 8U, OverlappingBorderPolicy::kWholeAxis),
             tile_x, whole_stretch, false, true);
  const auto overlap_both = MakeTheme("layout_horizontal=1,3,3\nlayout_vertical=0,3,3\n");
  ExpectGrid(overlap_both.Render(8U, 8U, OverlappingBorderPolicy::kWholeAxis),
             whole_tile, whole_stretch, true, true);
  ExpectFailure(stretch.Render(1U, 5U, OverlappingBorderPolicy::kWholeAxis),
                SogouBackgroundError::kInvalidGeometry);

  const auto decoded = DecodePngBitmap(kGridPng);
  SogouSurfaceBitmap scaled;
  Expect(decoded.ok() && PrepareSogouBitmapSurface(
             decoded.bitmap, BitmapSurfaceScale::kMitchell2x, &scaled), "scaled fixture must prepare");
  const auto doubled = stretch.Render(10U, 10U, OverlappingBorderPolicy::kReject,
                                       BitmapSurfaceScale::kMitchell2x);
  Expect(doubled.ok() && doubled.background.width == 10U && doubled.background.height == 10U,
         "source scaling must scale cap geometry");
  for (std::size_t y = 0; y < 10U; ++y) {
    for (std::size_t x = 0; x < 10U; ++x) {
      Expect(doubled.background.pixels[y * 10U + x] == scaled.pixels[(9U - y) * 10U + x],
             "native assembly of scaled source must only reorder rows");
    }
  }

  ExpectFailure(MakeTheme("").Render(5U, 5U), SogouBackgroundError::kMissingLayout);
  auto changed = stretch;
  changed.package.conversion.manifest.appearance.horizontal->background.reset();
  ExpectFailure(changed.Render(5U, 5U), SogouBackgroundError::kMissingH1Background);
  changed = stretch;
  changed.package.conversion.manifest.appearance.vertical =
      changed.package.conversion.manifest.appearance.horizontal;
  changed.package.conversion.manifest.appearance.horizontal.reset();
  ExpectFailure(changed.Render(5U, 5U), SogouBackgroundError::kMissingH1Background);
  changed = stretch;
  changed.package.conversion.manifest.appearance.horizontal->background->horizontal_layout =
      static_cast<ThemeImageLayout>(999);
  ExpectFailure(changed.Render(5U, 5U), SogouBackgroundError::kInvalidTheme);
  changed = stretch;
  changed.resources.source_package_sha256[0] = 'f';
  ExpectFailure(changed.Render(5U, 5U), SogouBackgroundError::kInvalidBinding);
  changed = stretch;
  changed.resources.assets.clear();
  ExpectFailure(changed.Render(5U, 5U), SogouBackgroundError::kInvalidBinding);
  changed = stretch;
  changed.resources.assets[0].source_path = "other.png";
  ExpectFailure(changed.Render(5U, 5U), SogouBackgroundError::kInvalidBinding);
  changed = stretch;
  changed.resources.assets[0].target_path = "assets/other.png";
  ExpectFailure(changed.Render(5U, 5U), SogouBackgroundError::kInvalidBinding);
  changed = stretch;
  changed.package.conversion.assets.push_back(changed.package.conversion.assets.front());
  changed.resources.assets.push_back(changed.resources.assets.front());
  ExpectFailure(changed.Render(5U, 5U), SogouBackgroundError::kInvalidBinding);
  changed = stretch;
  changed.resources.assets[0].bytes = {1U, 2U, 3U};
  const auto bad_png = changed.Render(5U, 5U);
  ExpectFailure(bad_png, SogouBackgroundError::kDecodeFailed);
  Expect(bad_png.decode_error == BitmapDecodeError::kUnsupportedFormat,
         "decoder error should remain distinguishable");

  BitmapDecodeLimits limits;
  limits.maximum_pixels = 24U;
  ExpectFailure(stretch.Render(5U, 5U, OverlappingBorderPolicy::kReject,
                                BitmapSurfaceScale::kUnscaled, limits),
                SogouBackgroundError::kResourceLimit);
  ExpectFailure(stretch.Render(0U, 5U), SogouBackgroundError::kInvalidGeometry);
  ExpectFailure(stretch.Render(5U, 5U, static_cast<OverlappingBorderPolicy>(999)),
                SogouBackgroundError::kInvalidGeometry);
  ExpectFailure(stretch.Render(5U, 5U, OverlappingBorderPolicy::kReject,
                                static_cast<BitmapSurfaceScale>(999)),
                SogouBackgroundError::kInvalidGeometry);
  ExpectFailure(stretch.Render(0xFFFFFFFFU, 0xFFFFFFFFU), SogouBackgroundError::kResourceLimit);
  return EXIT_SUCCESS;
}
