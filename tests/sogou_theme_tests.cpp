#include "ziliu/core/sogou_theme.h"

#include <array>
#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {

constexpr std::string_view kPackageSha =
    "0123456789abcdef0123456789abcdef"
    "0123456789abcdef0123456789abcdef";

void Expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

bool HasIssue(const ziliu::core::SogouThemeConversion& conversion,
              ziliu::core::SogouThemeIssueCode code,
              std::string_view path = {}) {
  for (const auto& issue : conversion.issues) {
    if (issue.code == code && (path.empty() || issue.path == path)) {
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

constexpr std::string_view kCompleteIni = R"ini(
[General]
skin_id=Paper Boat
skin_name=纸舟
skin_author=Ziliu Tests
skin_version=2.4
skin_info=Custom same-window mapping
preview_square=preview.gif

[Display]
font_size=18
font_ch=思源黑体
font_en=Segoe UI
use_gdip=1
pinyin_color=0x0080FF
zhongwen_color=1122867
zhongwen_first_color=0x030201
comphint_color=0x665544
caret_color=0x102030
zhongwen_first_bk_color=0xA0B0C0

[Scheme_H1]
anchor=11,47
pic=images\horizontal.bmp
layout_horizontal=0,38,192
layout_vertical=2,33,11
pinyin_marge=47,10,25,120
zhongwen_marge=10,5,15,90
separator=0xd8d8d8,20,100
pageup_display=1
pageup=up.png,unused.png
pageup_hover=up-hover.png
pageup_down=up-pressed.png
pagedown_display=0
pagedown=../disabled.png

[Scheme_V1]
anchor=-6,66
pic=images/horizontal.bmp
layout_horizontal=1,4,5
layout_vertical=0,6,7
custom_cnt=1
custom0_display=1
custom0=overlay.png
custom0_align=0,-1,2,-3,4,-5,6,-7,8,-9

[Scheme_H2]
pic=../../ignored.bmp
[StatusBar]
pic=ignored.exe
)ini";

}  // namespace

int main() {
  using namespace ziliu::core;

  const auto conversion =
      ConvertSogouThemeIni(kCompleteIni, "paper-boat.ssf", kPackageSha);
  Expect(conversion.ok(), "valid H1 and V1 custom data should convert");
  Expect(conversion.manifest.id == "sogou.paper-boat" &&
             conversion.manifest.name == "纸舟" &&
             conversion.manifest.author == "Ziliu Tests" &&
             conversion.manifest.version == "2.4" &&
             conversion.manifest.source_package_sha256 == kPackageSha &&
             conversion.manifest.source_format == "sogou-ssf" &&
             conversion.manifest.base_dpi == 96U,
         "source metadata and identity should map without platform state");
  const auto& appearance = conversion.manifest.appearance;
  Expect(appearance.typography.chinese_font_family == "思源黑体" &&
             appearance.typography.english_font_family == "Segoe UI" &&
             appearance.typography.font_size == 18U &&
             appearance.typography.text_renderer ==
                 ThemeTextRenderer::kSogouGdiPlus,
         "font metadata should map exactly when present");
  Expect(appearance.palette.preedit_text == 0xFFFF8000U &&
             appearance.palette.candidate_text == 0xFF332211U &&
             appearance.palette.highlighted_candidate_text == 0xFF010203U &&
             appearance.palette.annotation_text == 0xFF445566U &&
             appearance.palette.caret_text == 0xFF302010U &&
             appearance.palette.highlighted_background == 0xFFC0B0A0U,
         "24-bit BGR values should map to opaque ARGB roles");

  Expect(appearance.horizontal.has_value() &&
             appearance.horizontal->anchor == ThemePoint{11, 47} &&
             appearance.horizontal->background.has_value() &&
             appearance.horizontal->background->horizontal_layout ==
                 ThemeImageLayout::kStretch &&
             appearance.horizontal->background->vertical_layout ==
                 ThemeImageLayout::kFixed &&
             appearance.horizontal->background->stretch ==
                 ThemeInsets{38, 33, 192, 11} &&
             appearance.horizontal->preedit_insets ==
                 ThemeInsets{25, 47, 120, 10} &&
             appearance.horizontal->candidate_insets ==
                 ThemeInsets{15, 10, 90, 5},
         "H1 anchor, image layout and margins should map independently");
  Expect(appearance.horizontal->separator.has_value() &&
             appearance.horizontal->separator->color == 0xFFD8D8D8U &&
             appearance.horizontal->separator->left == 20U &&
             appearance.horizontal->separator->right == 100U &&
             appearance.horizontal->previous_button.has_value() &&
             !appearance.horizontal->next_button.has_value(),
         "separator and enabled pager resources should map without synthesis");

  Expect(appearance.vertical.has_value() &&
             appearance.vertical->anchor == ThemePoint{-6, 66} &&
             appearance.vertical->background.has_value() &&
             appearance.vertical->background->horizontal_layout ==
                 ThemeImageLayout::kTile &&
             appearance.vertical->background->vertical_layout ==
                 ThemeImageLayout::kStretch &&
             appearance.vertical->overlays.size() == 1U &&
             appearance.vertical->overlays.front().raw_alignment ==
                 std::array<std::int32_t, 10>{
                     0, -1, 2, -3, 4, -5, 6, -7, 8, -9},
         "V1 and opaque custom alignment data should remain lossless");
  Expect(conversion.assets.size() == 6U &&
             conversion.assets.front().source_path == "preview.gif" &&
             conversion.assets.front().target_path == "assets/ssf-000.png" &&
             !HasSourceAsset(conversion, "../../ignored.bmp") &&
             !HasSourceAsset(conversion, "ignored.exe"),
         "only referenced same-window assets should receive controlled paths");
  Expect(ValidateThemeManifest(conversion.manifest).empty(),
         "converted custom SSF data should satisfy the manifest boundary");

  const auto sparse = ConvertSogouThemeIni(
      "[General]\nskin_name=Sparse\n[Scheme_V1]\n", "sparse.ssf",
      kPackageSha);
  Expect(sparse.ok() && sparse.manifest.appearance.vertical.has_value() &&
             !sparse.manifest.appearance.vertical->background.has_value() &&
             !sparse.manifest.appearance.horizontal.has_value() &&
             !sparse.manifest.appearance.typography.font_size.has_value() &&
             !sparse.manifest.appearance.typography.text_renderer.has_value() &&
             !sparse.manifest.appearance.palette.preedit_text.has_value() &&
             !sparse.manifest.appearance.palette.highlighted_background
                  .has_value(),
         "missing custom fields must remain unset without injected appearance");

  const auto missing_layout = ConvertSogouThemeIni(
      "[General]\nskin_name=No Layout\n[Scheme_H1]\npic=skin.png\n",
      "no-layout.ssf", kPackageSha);
  Expect(missing_layout.ok() &&
             missing_layout.manifest.appearance.horizontal->background
                 .has_value() &&
             !missing_layout.manifest.appearance.horizontal->background
                  ->horizontal_layout.has_value() &&
             !missing_layout.manifest.appearance.horizontal->background
                  ->vertical_layout.has_value(),
         "missing image layout fields must not acquire guessed modes");

  const auto bad_number = ConvertSogouThemeIni(
      "[General]\nskin_name=Bad\n[Display]\nfont_size=large\n"
      "[Scheme_H1]\npic=skin.png\n",
      "bad.ssf", kPackageSha);
  Expect(!bad_number.ok() &&
             HasIssue(bad_number, SogouThemeIssueCode::kInvalidValue,
                      "Display.font_size"),
         "invalid numeric values should carry their source path");

  const auto bad_path = ConvertSogouThemeIni(
      "[General]\nskin_name=Bad Path\n[Scheme_H1]\npic=../skin.png\n",
      "bad-path.ssf", kPackageSha);
  Expect(!bad_path.ok() &&
             HasIssue(bad_path, SogouThemeIssueCode::kUnsafeAssetPath,
                      "Scheme_H1.pic"),
         "malicious image paths should be rejected");

  const auto bad_identity = ConvertSogouThemeIni(
      "[General]\nskin_name=Bad Identity\n[Scheme_H1]\n",
      "bad-identity.ssf", "not-a-sha");
  Expect(!bad_identity.ok() &&
             HasIssue(bad_identity, SogouThemeIssueCode::kInvalidValue,
                      "$.source_package_sha256"),
         "package identity must be an exact SHA-256 digest");

  const auto no_scheme = ConvertSogouThemeIni(
      "[General]\nskin_name=No Scheme\n", "no-scheme.ssf", kPackageSha);
  Expect(!no_scheme.ok() &&
             HasIssue(no_scheme, SogouThemeIssueCode::kMissingProperty, "$"),
         "H1 or V1 is required for a custom candidate surface");

  const auto duplicate = ConvertSogouThemeIni(
      "[General]\nskin_name=A\nSKIN_NAME=B\n[Scheme_H1]\n",
      "duplicate.ssf", kPackageSha);
  Expect(!duplicate.ok() &&
             HasIssue(duplicate, SogouThemeIssueCode::kDuplicateProperty),
         "case-insensitive duplicate properties should be rejected");

  return EXIT_SUCCESS;
}
