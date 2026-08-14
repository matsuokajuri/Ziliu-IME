#include "ziliu/core/theme_manifest.h"

#include <array>
#include <cstdint>
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

bool HasIssueAt(const std::vector<ziliu::core::ThemeManifestIssue>& issues,
                ziliu::core::ThemeManifestIssueCode code,
                std::string_view path) {
  for (const auto& issue : issues) {
    if (issue.code == code && issue.path == path) {
      return true;
    }
  }
  return false;
}

ziliu::core::ThemeManifest MakeManifest() {
  using namespace ziliu::core;

  ThemeManifest manifest;
  manifest.id = "sogou.paper-boat";
  manifest.name = "纸舟";
  manifest.author = "Ziliu Tests";
  manifest.version = "2.4";
  manifest.description = "Custom SSF manifest";
  manifest.preview_asset = "assets/preview.png";
  manifest.source_package_sha256 =
      "0123456789abcdef0123456789abcdef"
      "0123456789abcdef0123456789abcdef";
  manifest.appearance.typography.chinese_font_family = "思源黑体";
  manifest.appearance.typography.english_font_family = "Segoe UI";
  manifest.appearance.typography.font_size = 18U;
  manifest.appearance.typography.text_renderer =
      ThemeTextRenderer::kSogouGdiPlus;
  manifest.appearance.palette.preedit_text = 0xFFFF8000U;
  manifest.appearance.palette.candidate_text = 0xFF332211U;
  manifest.appearance.palette.highlighted_candidate_text = 0xFF010203U;
  manifest.appearance.palette.annotation_text = 0xFF445566U;
  manifest.appearance.palette.caret_text = 0xFF302010U;

  ThemeSurface horizontal;
  horizontal.anchor = {11, 47};
  horizontal.background = ThemeImage{"assets/horizontal.png",
                                     {38, 33, 192, 11},
                                     ThemeImageLayout::kStretch,
                                     ThemeImageLayout::kFixed};
  horizontal.preedit_insets = ThemeInsets{25, 47, 120, 10};
  horizontal.candidate_insets = ThemeInsets{15, 10, 90, 5};
  horizontal.separator = ThemeSeparator{0xFFD8D8D8U, 20, 100, 1};
  horizontal.previous_button =
      ThemeButtonImages{"assets/up.png", "assets/up-hover.png",
                        "assets/up-pressed.png"};
  manifest.appearance.horizontal = horizontal;

  ThemeSurface vertical;
  vertical.anchor = {-6, 66};
  vertical.overlays.push_back(
      {"assets/overlay.png", 0, 0,
       std::array<std::int32_t, 10>{0, -1, 2, -3, 4, -5, 6, -7, 8, -9}});
  manifest.appearance.vertical = vertical;
  return manifest;
}

}  // namespace

int main() {
  using namespace ziliu::core;

  const ThemeManifest complete = MakeManifest();
  Expect(ValidateThemeManifest(complete).empty(),
         "custom SSF fields should form a valid minimal manifest");
  const ThemeManifest value_copy = complete;
  Expect(value_copy == complete &&
             value_copy.appearance.vertical->overlays.front().raw_alignment ==
                 std::array<std::int32_t, 10>{
                     0, -1, 2, -3, 4, -5, 6, -7, 8, -9},
         "copying the model must preserve opaque custom alignment metadata");

  Expect(IsSafeThemeAssetPath("assets/skin.png") &&
             IsSafeThemeAssetPath("assets/中文.apng"),
         "normalized relative image paths should be accepted");
  for (const std::string_view unsafe :
       {"../skin.png", "assets/../skin.png", "assets\\skin.png",
        "C:/skin.png", "/skin.png", "assets//skin.png", "assets/./skin.png"}) {
    Expect(!IsSafeThemeAssetPath(unsafe),
           "absolute, traversal and non-normalized paths must be rejected");
  }

  ThemeManifest sparse = MakeManifest();
  sparse.preview_asset.clear();
  sparse.author.clear();
  sparse.version.clear();
  sparse.description.clear();
  sparse.appearance.palette = {};
  sparse.appearance.typography = {};
  sparse.appearance.vertical.reset();
  sparse.appearance.horizontal = ThemeSurface{};
  Expect(ValidateThemeManifest(sparse).empty() &&
             !sparse.appearance.palette.preedit_text.has_value() &&
             !sparse.appearance.typography.font_size.has_value() &&
             !sparse.appearance.horizontal->background.has_value(),
         "missing optional SSF fields must remain unset without appearance injection");

  ThemeManifest invalid = MakeManifest();
  invalid.source_package_sha256 = "not-a-sha";
  invalid.base_dpi = 144;
  invalid.appearance.typography.font_size = 7U;
  invalid.appearance.horizontal->background->asset = "../unsafe.png";
  invalid.appearance.horizontal->background->vertical_layout =
      static_cast<ThemeImageLayout>(99);
  const auto invalid_issues = ValidateThemeManifest(invalid);
  Expect(HasIssueAt(invalid_issues, ThemeManifestIssueCode::kInvalidProperty,
                    "$.source_package_sha256") &&
             HasIssueAt(invalid_issues,
                        ThemeManifestIssueCode::kInvalidProperty,
                        "$.base_dpi") &&
             HasIssueAt(invalid_issues,
                        ThemeManifestIssueCode::kInvalidProperty,
                        "$.appearance.typography.font_size") &&
             HasIssueAt(invalid_issues,
                        ThemeManifestIssueCode::kUnsafeAssetPath,
                        "$.appearance.horizontal.background.asset") &&
             HasIssueAt(invalid_issues,
                        ThemeManifestIssueCode::kInvalidProperty,
                        "$.appearance.horizontal.background.vertical_layout"),
         "identity, enum, geometry and path boundaries must fail closed");

  ThemeManifest no_scheme = MakeManifest();
  no_scheme.appearance.horizontal.reset();
  no_scheme.appearance.vertical.reset();
  Expect(HasIssueAt(ValidateThemeManifest(no_scheme),
                    ThemeManifestIssueCode::kMissingProperty,
                    "$.appearance"),
         "a custom SSF manifest must retain at least one H1 or V1 section");

  return EXIT_SUCCESS;
}
