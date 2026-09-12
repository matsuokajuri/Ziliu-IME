#include "ziliu/core/theme_catalog.h"
#include "ziliu/core/theme_manifest.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>

namespace {

void Expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

bool HasIssue(const ziliu::core::ThemeManifestParseResult& result,
              ziliu::core::ThemeManifestIssueCode code) {
  for (const auto& issue : result.issues) {
    if (issue.code == code) {
      return true;
    }
  }
  return false;
}

}  // namespace

int main() {
  Expect(ziliu::core::IsReservedThemeId(ziliu::core::kDefaultThemeId),
         "the built-in Ziliu theme identity is reserved");
  Expect(!ziliu::core::IsReservedThemeId("org.example.custom"),
         "a custom theme identity is not reserved");
  using ziliu::core::ThemeButtonImages;
  using ziliu::core::ThemeImage;
  using ziliu::core::ThemeImageLayout;
  using ziliu::core::ThemeInsets;
  using ziliu::core::ThemeManifestIssueCode;
  using ziliu::core::ThemeOverlay;
  using ziliu::core::ThemeSeparator;

  auto manifest = ziliu::core::MakeDefaultThemeManifest();
  manifest.name = "字流默认 🌊";
  manifest.homepage.clear();
  manifest.preview_asset = "preview.png";
  manifest.source_format = "zlt";
  manifest.light.horizontal.background =
      ThemeImage{"assets/horizontal.png", ThemeInsets{38, 33, 192, 11},
                 ThemeImageLayout::kStretch, ThemeImageLayout::kFixed};
  manifest.light.horizontal.overlays = {
      ThemeOverlay{"assets/overlay-back.png", 0, 10,
                   {0, -1, 2, -3, 4, -5, 6, -7, 8, -9}},
      ThemeOverlay{"assets/overlay-front.png", 3, 20,
                   {10, 11, 12, 13, 14, 15, 16, 17, 18, 19}}};
  manifest.light.horizontal.preedit_insets = ThemeInsets{25, 47, 120, 10};
  manifest.light.horizontal.candidate_insets = ThemeInsets{15, 10, 90, 5};
  manifest.light.horizontal.separator =
      ThemeSeparator{0xFFD8D8D8, "", 20, 100, 1};
  manifest.light.horizontal.previous_button =
      ThemeButtonImages{"assets/previous.png", "assets/previous-hover.png",
                        "assets/previous-pressed.png"};
  manifest.light.vertical.background =
      ThemeImage{"assets/vertical.apng", ThemeInsets{36, 66, 73, 14},
                 ThemeImageLayout::kTile, ThemeImageLayout::kStretch};
  manifest.light.vertical.preedit_insets = ThemeInsets{15, 24, 60, 3};
  manifest.light.vertical.candidate_insets = ThemeInsets{15, 20, 60, 20};
  manifest.dark->palette.highlighted_background = 0x80123A59;

  const auto validation = ziliu::core::ValidateThemeManifest(manifest);
  Expect(validation.empty(), "a complete first-party manifest should validate");

  const std::string serialized = ziliu::core::SerializeThemeManifest(manifest);
  Expect(serialized.find("\"format_version\": 1") != std::string::npos &&
             serialized.find("\"highlighted_background\": \"#123A5980\"") !=
                 std::string::npos &&
             serialized.find("\"horizontal\": \"tile\"") != std::string::npos &&
             serialized.find("\"vertical\": \"fixed\"") !=
                 std::string::npos &&
             serialized.find("\"custom_index\": 3") != std::string::npos &&
             serialized.find(
                 "\"align\": [0, -1, 2, -3, 4, -5, 6, -7, 8, -9]") !=
                 std::string::npos &&
             serialized.find("\"preedit\": [25, 47, 120, 10]") !=
                 std::string::npos &&
             serialized.find("\"color\": \"#D8D8D8\"") != std::string::npos,
         "serialization should use stable v1 JSON values");

  const auto parsed = ziliu::core::ParseThemeManifest(serialized);
  Expect(parsed.ok(), "serialized manifest should parse");
  Expect(parsed.manifest == manifest, "manifest should survive a deterministic round trip");
  Expect(ziliu::core::SerializeThemeManifest(parsed.manifest) == serialized,
         "canonical serialization should be byte-stable");

  std::string extended = serialized;
  extended.insert(1, "\n  \"future_extension\": {\"enabled\": true},");
  const auto parsed_extended = ziliu::core::ParseThemeManifest(extended);
  Expect(parsed_extended.ok() && parsed_extended.manifest == manifest,
         "unknown properties should be ignored for forward compatibility");

  auto unsafe = manifest;
  unsafe.preview_asset = "../preview.png";
  unsafe.light.horizontal.background->asset = "C:/theme.png";
  unsafe.light.horizontal.separator->asset = "../separator.png";
  const auto unsafe_issues = ziliu::core::ValidateThemeManifest(unsafe);
  Expect(unsafe_issues.size() == 3,
         "validation should report each unsafe asset reference");
  Expect(!ziliu::core::IsSafeThemeAssetPath("../image.png") &&
             !ziliu::core::IsSafeThemeAssetPath("/image.png") &&
             !ziliu::core::IsSafeThemeAssetPath("assets\\image.png") &&
             !ziliu::core::IsSafeThemeAssetPath("assets//image.png") &&
             ziliu::core::IsSafeThemeAssetPath("assets/主题/image.png"),
         "asset paths should reject traversal and Windows-specific paths");

  auto invalid_separator = manifest;
  invalid_separator.light.horizontal.separator->thickness = 0;
  const auto invalid_separator_issues =
      ziliu::core::ValidateThemeManifest(invalid_separator);
  Expect(invalid_separator_issues.size() == 1 &&
             invalid_separator_issues.front().path ==
                 "$.appearances.light.surfaces.horizontal.separator.thickness",
         "separator geometry should be validated deterministically");

  auto invalid_layout = manifest;
  invalid_layout.light.horizontal.background->vertical_layout =
      static_cast<ThemeImageLayout>(99);
  const auto invalid_layout_issues =
      ziliu::core::ValidateThemeManifest(invalid_layout);
  Expect(invalid_layout_issues.size() == 1 &&
             invalid_layout_issues.front().path ==
                 "$.appearances.light.surfaces.horizontal.background.layout.vertical",
         "unknown in-memory image layout values should fail validation");

  auto too_many_overlays = manifest;
  too_many_overlays.light.horizontal.overlays.assign(
      ziliu::core::kMaximumThemeSurfaceOverlays + 1,
      manifest.light.horizontal.overlays.front());
  const auto too_many_overlay_issues =
      ziliu::core::ValidateThemeManifest(too_many_overlays);
  Expect(too_many_overlay_issues.size() == 1 &&
             too_many_overlay_issues.front().path ==
                 "$.appearances.light.surfaces.horizontal.overlays",
         "surface overlay count limits should be validated deterministically");

  const auto unsupported =
      ziliu::core::ParseThemeManifest(std::string(serialized).replace(
          serialized.find("\"format_version\": 1"),
          std::string_view("\"format_version\": 1").size(), "\"format_version\": 2"));
  Expect(!unsupported.ok() && HasIssue(unsupported, ThemeManifestIssueCode::kUnsupportedVersion),
         "unsupported manifest versions should fail explicitly");

  const auto duplicate = ziliu::core::ParseThemeManifest("{\"id\":\"a\",\"id\":\"b\"}");
  Expect(!duplicate.ok() && HasIssue(duplicate, ThemeManifestIssueCode::kInvalidJson),
         "duplicate JSON properties should be rejected");

  const auto missing = ziliu::core::ParseThemeManifest("{}");
  Expect(!missing.ok() && HasIssue(missing, ThemeManifestIssueCode::kMissingProperty),
         "required manifest properties should be reported");

  const std::string oversized(ziliu::core::kMaximumThemeManifestBytes + 1, ' ');
  const auto too_large = ziliu::core::ParseThemeManifest(oversized);
  Expect(!too_large.ok() && HasIssue(too_large, ThemeManifestIssueCode::kManifestTooLarge),
         "oversized manifests should be rejected before parsing");

  const auto malformed = ziliu::core::ParseThemeManifest("{\"format_version\": 1,}");
  Expect(!malformed.ok() && HasIssue(malformed, ThemeManifestIssueCode::kInvalidJson),
         "malformed JSON should return a syntax issue");

  const std::filesystem::path example_path =
      std::filesystem::path(__FILE__).parent_path().parent_path() / "examples" / "themes" /
      "minimal" / "manifest.json";
  std::ifstream example_stream(example_path, std::ios::binary);
  const std::string example((std::istreambuf_iterator<char>(example_stream)),
                            std::istreambuf_iterator<char>());
  Expect(example_stream.good() || example_stream.eof(),
         "the checked-in minimal manifest should be readable");
  Expect(ziliu::core::ParseThemeManifest(example).ok(),
         "the checked-in minimal manifest should stay compatible with the parser");

  const std::filesystem::path catalog_root =
      std::filesystem::temp_directory_path() / "ziliu-theme-catalog-tests";
  std::error_code catalog_error;
  std::filesystem::remove_all(catalog_root, catalog_error);
  const std::filesystem::path installed_directory =
      catalog_root / std::filesystem::path(manifest.id);
  std::filesystem::create_directories(installed_directory, catalog_error);
  Expect(!catalog_error, "theme catalog test directory should be created");
  std::ofstream installed_manifest(installed_directory / "manifest.json",
                                   std::ios::binary | std::ios::trunc);
  installed_manifest.write(serialized.data(),
                           static_cast<std::streamsize>(serialized.size()));
  installed_manifest.close();
  const auto installed_themes = ziliu::core::LoadInstalledThemes(catalog_root);
  const auto installed_theme =
      ziliu::core::LoadInstalledTheme(catalog_root, manifest.id);
  Expect(installed_themes.size() == 1 && installed_theme.has_value() &&
             installed_theme->manifest == manifest &&
             !ziliu::core::LoadInstalledTheme(catalog_root, "../unsafe").has_value(),
         "theme catalog should load only validated id-matching directories");
  std::filesystem::remove_all(catalog_root, catalog_error);

  std::cout << "ziliu_theme_manifest_tests: OK\n";
  return EXIT_SUCCESS;
}
