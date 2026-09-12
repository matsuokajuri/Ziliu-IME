#include "ziliu/core/sogou_theme.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

namespace {

void Expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

bool HasIssue(const ziliu::core::SogouThemeConversion& conversion,
              ziliu::core::SogouThemeIssueCode code) {
  for (const auto& issue : conversion.issues) {
    if (issue.code == code) {
      return true;
    }
  }
  return false;
}

bool HasIssueAt(const ziliu::core::SogouThemeConversion& conversion,
                ziliu::core::SogouThemeIssueCode code,
                std::string_view path) {
  for (const auto& issue : conversion.issues) {
    if (issue.code == code && issue.path == path) {
      return true;
    }
  }
  return false;
}

bool HasSourceAsset(const ziliu::core::SogouThemeConversion& conversion,
                    std::string_view source) {
  for (const auto& asset : conversion.assets) {
    if (asset.source_path == source) {
      return true;
    }
  }
  return false;
}

constexpr std::string_view kValidIni = R"ini(
[General]
skin_id=My Theme
skin_name=纸舟
skin_author=Ziliu Tests
skin_version=2.4
skin_info=同窗映射测试
preview_square=preview.gif

[Display]
font_size=18
font_ch=思源黑体
font_en=Segoe UI
pinyin_color=0x0080FF
zhongwen_first_color=0x030201
zhongwen_color=1122867
comphint_color=0x665544

[Scheme_H1]
pic=images\horizontal.bmp
layout_horizontal=0,38,192
layout_vertical=2,33,11
pinyin_marge=47,10,25,120
zhongwen_marge=10,5,15,90
separator=0xd8d8d8,20,100
pageup_display=1
pageup=up.png,up-disabled.png
pageup_hover=up-hover.png,up-disabled.png
pageup_down=up-pressed.png,up-disabled.png
pagedown_display=0
pagedown=../ignored-because-disabled.png

[Scheme_V1]
pic=images/horizontal.bmp
layout_horizontal=1,4,5
layout_vertical=0,6,7
pinyin_marge=8,9,10,11
zhongwen_marge=12,13,14,15
custom_cnt=2
custom0_display=1
custom0=ov_custom01.png
custom0_align=0,-1,2,-3,4,-5,6,-7,8,-9
custom1_display=0
custom1=../ignored-disabled-overlay.png
custom1_align=not,validated,when,disabled

[Scheme_H2]
pic=../../must-not-be-imported.bmp
layout_horizontal=not,a,number

[Scheme_V2]
pic=C:\must-not-be-imported.bmp

[StatusBar]
pic=also-ignored.exe
)ini";

}  // namespace

int main() {
  using ziliu::core::SogouThemeIssueCode;
  using ziliu::core::ThemeImageLayout;
  using ziliu::core::ThemeInsets;

  const auto conversion =
      ziliu::core::ConvertSogouThemeIni(kValidIni, "纸舟.ssf");
  Expect(conversion.ok(), "a complete same-window SSF mapping should convert");
  Expect(conversion.manifest.id == "sogou.my-theme" &&
             conversion.manifest.name == "纸舟" &&
             conversion.manifest.author == "Ziliu Tests" &&
             conversion.manifest.version == "2.4" &&
             conversion.manifest.license == "LicenseRef-Unknown" &&
             conversion.manifest.description == "同窗映射测试" &&
             conversion.manifest.source_format == "sogou-ssf" &&
             conversion.manifest.base_dpi == 96,
         "General metadata should map to the ZLT manifest");
  Expect(conversion.manifest.light.typography.chinese_font_family ==
                 "思源黑体" &&
             conversion.manifest.light.typography.english_font_family ==
                 "Segoe UI" &&
             conversion.manifest.light.typography.font_size == 18,
         "Display fonts should map without changing family names");
  Expect(conversion.manifest.light.palette.preedit_text == 0xFFFF8000 &&
             conversion.manifest.light.palette.highlighted_candidate_text ==
                 0xFF010203 &&
             conversion.manifest.light.palette.candidate_text == 0xFF332211 &&
             conversion.manifest.light.palette.muted_text == 0xFF445566 &&
             conversion.manifest.light.palette.highlighted_background ==
                 0x00000000,
         "Sogou BGR integers should map to ARGB and selection fill should stay transparent");

  const auto& horizontal = conversion.manifest.light.horizontal;
  Expect(horizontal.background.has_value() &&
             horizontal.background->horizontal_layout ==
                 ThemeImageLayout::kStretch &&
             horizontal.background->vertical_layout ==
                 ThemeImageLayout::kFixed &&
             horizontal.background->stretch ==
                 ThemeInsets{38, 33, 192, 11},
         "H1 background layouts and nine-slice insets should map");
  Expect(horizontal.preedit_insets == ThemeInsets{25, 47, 120, 10} &&
             horizontal.candidate_insets == ThemeInsets{15, 10, 90, 5},
         "SSF top,bottom,left,right margins should become ZLT left,top,right,bottom");
  Expect(horizontal.separator.has_value() &&
             horizontal.separator->color == 0xFFD8D8D8 &&
             horizontal.separator->left == 20 &&
             horizontal.separator->right == 100 &&
             horizontal.separator->thickness == 1,
         "separator color and geometry should map");
  Expect(horizontal.previous_button.has_value() &&
             !horizontal.previous_button->normal.empty() &&
             !horizontal.previous_button->hover.empty() &&
             !horizontal.previous_button->pressed.empty() &&
             !horizontal.next_button.has_value(),
         "enabled page buttons should map their first image state and disabled buttons should not");

  const auto& vertical = conversion.manifest.light.vertical;
  Expect(vertical.background.has_value() &&
             vertical.background->asset == horizontal.background->asset &&
             vertical.background->horizontal_layout == ThemeImageLayout::kTile &&
             vertical.background->vertical_layout ==
                 ThemeImageLayout::kStretch,
         "V1 should map independently while shared source images are deduplicated");
  Expect(vertical.overlays.size() == 1 &&
             vertical.overlays.front().custom_index == 0 &&
             vertical.overlays.front().draw_order == 0 &&
             vertical.overlays.front().align ==
                 std::array<std::int32_t, 10>{
                     0, -1, 2, -3, 4, -5, 6, -7, 8, -9} &&
             HasSourceAsset(conversion, "ov_custom01.png") &&
             !HasSourceAsset(conversion,
                             "../ignored-disabled-overlay.png"),
         "only enabled custom overlays should register assets and preserve all 10 alignment integers");
  Expect(conversion.manifest.preview_asset == "assets/ssf-000.png" &&
             conversion.assets.size() == 6 &&
             conversion.assets.front().source_path == "preview.gif",
         "referenced images including GIF should receive deterministic controlled PNG targets");
  for (const auto& asset : conversion.assets) {
    Expect(asset.target_path.starts_with("assets/ssf-") &&
               asset.target_path.ends_with(".png"),
           "every converted image destination should be a sanitized PNG path");
  }
  Expect(!HasSourceAsset(conversion, "../../must-not-be-imported.bmp") &&
             !HasSourceAsset(conversion, "also-ignored.exe"),
         "H2, V2 and StatusBar assets should be ignored");
  Expect(ziliu::core::ValidateThemeManifest(conversion.manifest).empty(),
         "the produced manifest should pass canonical validation");

  constexpr std::string_view fallback_ini = R"ini(
[General]
skin_name=No Stable Id
[Scheme_H1]
pic=skin.png
)ini";
  const auto fallback_a =
      ziliu::core::ConvertSogouThemeIni(fallback_ini, "fallback.ssf");
  const auto fallback_b =
      ziliu::core::ConvertSogouThemeIni(fallback_ini, "fallback.ssf");
  const auto fallback_changed = ziliu::core::ConvertSogouThemeIni(
      std::string(fallback_ini) + "\n[Extra]\nvalue=changed\n",
      "fallback.ssf");
  Expect(fallback_a.ok() && fallback_b.ok() && fallback_changed.ok() &&
             fallback_a.manifest.id == fallback_b.manifest.id &&
             fallback_a.manifest.id != fallback_changed.manifest.id &&
             fallback_a.manifest.id.starts_with("sogou.no-stable-id-"),
         "missing skin_id should receive a stable content-derived fallback id");

  const auto aliases = ziliu::core::ConvertSogouThemeIni(
      "[General]\nname=Alias Name\nauthor=Alias Author\nversion=3\n"
      "info=Alias Info\n[Scheme_V1]\npic=skin.png\n",
      "alias.ssf");
  Expect(aliases.ok() && aliases.manifest.name == "Alias Name" &&
             aliases.manifest.author == "Alias Author" &&
             aliases.manifest.version == "3" &&
             aliases.manifest.description == "Alias Info",
         "common legacy General aliases should remain compatible");

  const auto duplicate = ziliu::core::ConvertSogouThemeIni(
      "[General]\nskin_name=A\nSKIN_NAME=B\n"
      "[Scheme_H1]\npic=skin.png\n",
      "duplicate.ssf");
  Expect(!duplicate.ok() &&
             HasIssue(duplicate, SogouThemeIssueCode::kDuplicateProperty),
         "case-insensitive duplicate properties should be rejected");

  const auto duplicate_section = ziliu::core::ConvertSogouThemeIni(
      "[Scheme_H1]\npic=a.png\n[scheme_h1]\npic=b.png\n",
      "duplicate-section.ssf");
  Expect(!duplicate_section.ok() &&
             HasIssue(duplicate_section,
                      SogouThemeIssueCode::kDuplicateProperty),
         "case-insensitive duplicate sections should be rejected");

  const auto unsafe = ziliu::core::ConvertSogouThemeIni(
      "[Scheme_H1]\npic=../escape.png\n", "unsafe.ssf");
  Expect(!unsafe.ok() &&
             HasIssue(unsafe, SogouThemeIssueCode::kUnsafeAssetPath),
         "unsafe referenced source paths should be rejected");

  const auto reserved_path = ziliu::core::ConvertSogouThemeIni(
      "[Scheme_H1]\npic=assets/CON.png\n", "reserved.ssf");
  Expect(!reserved_path.ok() &&
             HasIssue(reserved_path, SogouThemeIssueCode::kUnsafeAssetPath),
         "Windows reserved filenames should be rejected before extraction");

  const auto enabled_without_image = ziliu::core::ConvertSogouThemeIni(
      "[Scheme_H1]\npic=skin.png\npageup_display=1\n",
      "missing-button.ssf");
  Expect(!enabled_without_image.ok() &&
             HasIssue(enabled_without_image,
                      SogouThemeIssueCode::kMissingProperty),
         "an explicitly enabled button without an image should be rejected");

  const auto bad_layout = ziliu::core::ConvertSogouThemeIni(
      "[Scheme_H1]\npic=skin.png\nlayout_horizontal=0,1,nope\n",
      "bad-layout.ssf");
  Expect(!bad_layout.ok() &&
             HasIssue(bad_layout, SogouThemeIssueCode::kInvalidValue),
         "malformed layout numbers should be rejected");

  const auto unsupported_horizontal_layout =
      ziliu::core::ConvertSogouThemeIni(
          "[Scheme_H1]\npic=skin.png\nlayout_horizontal=2,1,2\n",
          "unsupported-horizontal-layout.ssf");
  Expect(!unsupported_horizontal_layout.ok() &&
             HasIssueAt(unsupported_horizontal_layout,
                        SogouThemeIssueCode::kInvalidValue,
                        "Scheme_H1.layout_horizontal"),
         "Sogou horizontal layout mode 2 should be rejected instead of becoming tile");

  const auto unsupported_vertical_layout =
      ziliu::core::ConvertSogouThemeIni(
          "[Scheme_H1]\npic=skin.png\nlayout_vertical=3,1,2\n",
          "unsupported-vertical-layout.ssf");
  Expect(!unsupported_vertical_layout.ok() &&
             HasIssueAt(unsupported_vertical_layout,
                        SogouThemeIssueCode::kInvalidValue,
                        "Scheme_H1.layout_vertical"),
         "unknown Sogou vertical layout modes should be rejected");

  const auto legacy_one_based_overlay =
      ziliu::core::ConvertSogouThemeIni(
          "[Scheme_H1]\npic=skin.png\ncustom_cnt=1\n"
          "custom1_display=1\ncustom1=legacy.png\n"
          "custom1_align=1,2,3,4,5,6,7,8,9,10\n",
          "legacy-one-based-overlay.ssf");
  Expect(legacy_one_based_overlay.ok() &&
             legacy_one_based_overlay.manifest.light.horizontal.overlays.size() ==
                 1 &&
             legacy_one_based_overlay.manifest.light.horizontal.overlays.front()
                     .custom_index == 1 &&
             HasSourceAsset(legacy_one_based_overlay, "legacy.png"),
         "legacy one-based custom overlay indexes should remain importable");

  const auto disabled_overlay = ziliu::core::ConvertSogouThemeIni(
      "[Scheme_H1]\npic=skin.png\ncustom_cnt=1\n"
      "custom0_display=0\ncustom0=../unsafe-but-disabled.png\n"
      "custom0_align=malformed\n",
      "disabled-overlay.ssf");
  Expect(disabled_overlay.ok() &&
             disabled_overlay.manifest.light.horizontal.overlays.empty() &&
             !HasSourceAsset(disabled_overlay, "../unsafe-but-disabled.png"),
         "disabled custom overlays should not register or validate dormant resources");

  const auto enabled_overlay_without_alignment =
      ziliu::core::ConvertSogouThemeIni(
          "[Scheme_H1]\npic=skin.png\ncustom_cnt=1\n"
          "custom0_display=1\ncustom0=overlay.png\n",
          "missing-overlay-alignment.ssf");
  Expect(!enabled_overlay_without_alignment.ok() &&
             HasIssueAt(enabled_overlay_without_alignment,
                        SogouThemeIssueCode::kMissingProperty,
                        "Scheme_H1.custom0_align"),
         "enabled custom overlays should require their 10-value alignment tuple");

  const auto invalid_overlay_count =
      ziliu::core::ConvertSogouThemeIni(
          "[Scheme_H1]\npic=skin.png\ncustom_cnt=129\n",
          "invalid-overlay-count.ssf");
  Expect(!invalid_overlay_count.ok() &&
             HasIssueAt(invalid_overlay_count,
                        SogouThemeIssueCode::kInvalidValue,
                        "Scheme_H1.custom_cnt"),
         "out-of-range custom overlay counts should report their exact field");

  const auto bad_color = ziliu::core::ConvertSogouThemeIni(
      "[Display]\npinyin_color=0x1000000\n"
      "[Scheme_H1]\npic=skin.png\n",
      "bad-color.ssf");
  Expect(!bad_color.ok() &&
             HasIssue(bad_color, SogouThemeIssueCode::kInvalidValue),
         "colors wider than 24 bits should be rejected");

  const auto bad_font_size = ziliu::core::ConvertSogouThemeIni(
      "[Display]\nfont_size=97\n[Scheme_H1]\npic=skin.png\n",
      "bad-font.ssf");
  Expect(!bad_font_size.ok() &&
             HasIssue(bad_font_size, SogouThemeIssueCode::kInvalidValue),
         "out-of-range font sizes should be rejected");

  const auto missing_scheme = ziliu::core::ConvertSogouThemeIni(
      "[General]\nskin_name=No Surface\n", "missing.ssf");
  Expect(!missing_scheme.ok() &&
             HasIssue(missing_scheme, SogouThemeIssueCode::kMissingProperty),
         "a skin without H1 or V1 should be rejected");

  std::string invalid_utf8 =
      "[Scheme_H1]\npic=skin.png\n[General]\nskin_name=";
  invalid_utf8.push_back(static_cast<char>(0xC3));
  invalid_utf8.push_back(static_cast<char>(0x28));
  const auto bad_utf8 =
      ziliu::core::ConvertSogouThemeIni(invalid_utf8, "utf8.ssf");
  Expect(!bad_utf8.ok() &&
             HasIssue(bad_utf8, SogouThemeIssueCode::kInvalidUtf8),
         "invalid UTF-8 should be rejected before INI parsing");

  const std::string oversized(ziliu::core::kMaximumSogouThemeIniBytes + 1,
                              ' ');
  const auto too_large =
      ziliu::core::ConvertSogouThemeIni(oversized, "large.ssf");
  Expect(!too_large.ok() &&
             HasIssue(too_large, SogouThemeIssueCode::kIniTooLarge),
         "oversized skin.ini documents should be rejected before parsing");

  std::cout << "ziliu_sogou_theme_tests: OK\n";
  return EXIT_SUCCESS;
}
