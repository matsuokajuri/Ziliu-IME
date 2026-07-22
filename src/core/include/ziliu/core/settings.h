#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace ziliu::core {

enum class CandidateLayout {
  kVertical,
  kHorizontal,
};

enum class InputModeSwitchKey {
  kShift,
  kControl,
};

enum class PunctuationStyle {
  kFullWidth,
  kHalfWidth,
};

enum class PageKeySet {
  kCommaPeriod,
  kSemicolonApostrophe,
  kBrackets,
};

enum class CharacterSet {
  kSimplified,
  kTraditional,
};

enum class DefaultInputMode {
  kChinese,
  kEnglish,
};

enum class ThemeMode {
  kSystem,
  kLight,
  kDark,
};

enum class CandidatePageMode {
  kSingleLine,
  kMultiLine,
};

enum class CandidateChineseFontFamily {
  kSourceHanSans,
  kMicrosoftYaHei,
  kSimSun,
};

enum class CandidateEnglishFontFamily {
  kSegoeUi,
  kArial,
  kSourceHanSans,
};

struct Settings {
  CandidateLayout candidate_layout = CandidateLayout::kVertical;
  std::size_t candidate_count = 5;
  InputModeSwitchKey input_mode_switch_key = InputModeSwitchKey::kShift;
  PunctuationStyle punctuation_style = PunctuationStyle::kFullWidth;
  bool auto_pair_punctuation = true;
  PageKeySet page_key_set = PageKeySet::kCommaPeriod;
  CharacterSet character_set = CharacterSet::kSimplified;
  DefaultInputMode default_input_mode = DefaultInputMode::kChinese;
  bool initialism_spelling = true;
  bool spelling_correction = true;
  bool correction_gn_ng = true;
  bool correction_mg_ng = true;
  bool correction_iou_iu = true;
  bool correction_uei_ui = true;
  bool correction_uen_un = true;
  bool fuzzy_z_zh = false;
  bool fuzzy_c_ch = false;
  bool fuzzy_s_sh = false;
  bool fuzzy_l_n = false;
  bool fuzzy_f_h = false;
  bool fuzzy_r_l = false;
  bool fuzzy_an_ang = false;
  bool fuzzy_en_eng = false;
  bool fuzzy_in_ing = false;
  bool fuzzy_ian_iang = false;
  bool fuzzy_uan_uang = false;
  bool smart_numeric_punctuation = true;
  ThemeMode theme_mode = ThemeMode::kSystem;
  CandidatePageMode candidate_page_mode = CandidatePageMode::kSingleLine;
  bool custom_candidate_colors = false;
  std::uint32_t preedit_color = 0x202124;
  std::uint32_t highlighted_candidate_color = 0x0067C0;
  std::uint32_t candidate_text_color = 0x202124;
  std::uint32_t candidate_background_color = 0xFAFAFA;
  bool custom_candidate_fonts = false;
  CandidateChineseFontFamily candidate_chinese_font_family =
      CandidateChineseFontFamily::kSourceHanSans;
  CandidateEnglishFontFamily candidate_english_font_family =
      CandidateEnglishFontFamily::kSegoeUi;
  bool custom_candidate_font_size = false;
  std::size_t candidate_font_size = 17;
  bool candidate_scale_with_text = true;

  bool operator==(const Settings&) const = default;
};

struct CandidatePageSlice {
  std::size_t offset = 0;
  std::size_t count = 0;

  bool operator==(const CandidatePageSlice&) const = default;
};

inline constexpr std::size_t kMinimumCandidateCount = 3;
inline constexpr std::size_t kMaximumCandidateCount = 9;
inline constexpr std::size_t kMinimumCandidateFontSize = 14;
inline constexpr std::size_t kMaximumCandidateFontSize = 24;

[[nodiscard]] Settings ParseSettings(std::string_view text);
[[nodiscard]] std::string SerializeSettings(const Settings& settings);
[[nodiscard]] CandidatePageSlice MakeCandidatePageSlice(std::size_t candidate_total,
                                                        std::size_t page_size,
                                                        std::size_t requested_offset);

}  // namespace ziliu::core
