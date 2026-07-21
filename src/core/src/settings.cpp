#include "ziliu/core/settings.h"

#include <algorithm>
#include <charconv>
#include <string>

namespace ziliu::core {
namespace {

std::string_view Trim(std::string_view value) {
  while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
    value.remove_prefix(1);
  }
  while (!value.empty() &&
         (value.back() == ' ' || value.back() == '\t' || value.back() == '\r')) {
    value.remove_suffix(1);
  }
  return value;
}

void ParseBoolean(std::string_view value, bool* destination) {
  if (value == "true") {
    *destination = true;
  } else if (value == "false") {
    *destination = false;
  }
}

}  // namespace

Settings ParseSettings(std::string_view text) {
  Settings settings;
  while (!text.empty()) {
    const std::size_t line_end = text.find('\n');
    const std::string_view line = Trim(text.substr(0, line_end));
    text = line_end == std::string_view::npos ? std::string_view{} : text.substr(line_end + 1);
    if (line.empty() || line.front() == '#') {
      continue;
    }

    const std::size_t separator = line.find('=');
    if (separator == std::string_view::npos) {
      continue;
    }
    const std::string_view key = Trim(line.substr(0, separator));
    const std::string_view value = Trim(line.substr(separator + 1));
    if (key == "candidate_layout") {
      if (value == "vertical") {
        settings.candidate_layout = CandidateLayout::kVertical;
      } else if (value == "horizontal") {
        settings.candidate_layout = CandidateLayout::kHorizontal;
      }
    } else if (key == "candidate_count") {
      std::size_t count = settings.candidate_count;
      const auto result = std::from_chars(value.data(), value.data() + value.size(), count);
      if (result.ec == std::errc{} && result.ptr == value.data() + value.size()) {
        settings.candidate_count =
            std::clamp(count, kMinimumCandidateCount, kMaximumCandidateCount);
      }
    } else if (key == "input_mode_switch_key") {
      if (value == "shift") {
        settings.input_mode_switch_key = InputModeSwitchKey::kShift;
      } else if (value == "control") {
        settings.input_mode_switch_key = InputModeSwitchKey::kControl;
      }
    } else if (key == "punctuation_style") {
      if (value == "full_width") {
        settings.punctuation_style = PunctuationStyle::kFullWidth;
      } else if (value == "half_width") {
        settings.punctuation_style = PunctuationStyle::kHalfWidth;
      }
    } else if (key == "auto_pair_punctuation") {
      if (value == "true") {
        settings.auto_pair_punctuation = true;
      } else if (value == "false") {
        settings.auto_pair_punctuation = false;
      }
    } else if (key == "page_keys") {
      if (value == "comma_period") {
        settings.page_key_set = PageKeySet::kCommaPeriod;
      } else if (value == "semicolon_apostrophe") {
        settings.page_key_set = PageKeySet::kSemicolonApostrophe;
      } else if (value == "brackets") {
        settings.page_key_set = PageKeySet::kBrackets;
      }
    } else if (key == "character_set") {
      if (value == "simplified") {
        settings.character_set = CharacterSet::kSimplified;
      } else if (value == "traditional") {
        settings.character_set = CharacterSet::kTraditional;
      }
    } else if (key == "default_input_mode") {
      if (value == "chinese") {
        settings.default_input_mode = DefaultInputMode::kChinese;
      } else if (value == "english") {
        settings.default_input_mode = DefaultInputMode::kEnglish;
      }
    } else if (key == "initialism_spelling") {
      ParseBoolean(value, &settings.initialism_spelling);
    } else if (key == "spelling_correction") {
      ParseBoolean(value, &settings.spelling_correction);
    } else if (key == "correction_gn_ng") {
      ParseBoolean(value, &settings.correction_gn_ng);
    } else if (key == "correction_mg_ng") {
      ParseBoolean(value, &settings.correction_mg_ng);
    } else if (key == "correction_iou_iu") {
      ParseBoolean(value, &settings.correction_iou_iu);
    } else if (key == "correction_uei_ui") {
      ParseBoolean(value, &settings.correction_uei_ui);
    } else if (key == "correction_uen_un") {
      ParseBoolean(value, &settings.correction_uen_un);
    } else if (key == "fuzzy_z_zh") {
      ParseBoolean(value, &settings.fuzzy_z_zh);
    } else if (key == "fuzzy_c_ch") {
      ParseBoolean(value, &settings.fuzzy_c_ch);
    } else if (key == "fuzzy_s_sh") {
      ParseBoolean(value, &settings.fuzzy_s_sh);
    } else if (key == "fuzzy_l_n") {
      ParseBoolean(value, &settings.fuzzy_l_n);
    } else if (key == "fuzzy_f_h") {
      ParseBoolean(value, &settings.fuzzy_f_h);
    } else if (key == "fuzzy_r_l") {
      ParseBoolean(value, &settings.fuzzy_r_l);
    } else if (key == "fuzzy_an_ang") {
      ParseBoolean(value, &settings.fuzzy_an_ang);
    } else if (key == "fuzzy_en_eng") {
      ParseBoolean(value, &settings.fuzzy_en_eng);
    } else if (key == "fuzzy_in_ing") {
      ParseBoolean(value, &settings.fuzzy_in_ing);
    } else if (key == "fuzzy_ian_iang") {
      ParseBoolean(value, &settings.fuzzy_ian_iang);
    } else if (key == "fuzzy_uan_uang") {
      ParseBoolean(value, &settings.fuzzy_uan_uang);
    } else if (key == "smart_numeric_punctuation") {
      ParseBoolean(value, &settings.smart_numeric_punctuation);
    } else if (key == "theme_mode") {
      if (value == "light") {
        settings.theme_mode = ThemeMode::kLight;
      } else if (value == "dark") {
        settings.theme_mode = ThemeMode::kDark;
      } else if (value == "system") {
        settings.theme_mode = ThemeMode::kSystem;
      }
    } else if (key == "candidate_page_mode") {
      if (value == "single_line") {
        settings.candidate_page_mode = CandidatePageMode::kSingleLine;
      } else if (value == "multi_line") {
        settings.candidate_page_mode = CandidatePageMode::kMultiLine;
      }
    } else if (key == "candidate_font_family") {
      if (value == "source_han_sans") {
        settings.candidate_font_family = CandidateFontFamily::kSourceHanSans;
      } else if (value == "microsoft_yahei") {
        settings.candidate_font_family = CandidateFontFamily::kMicrosoftYaHei;
      } else if (value == "system") {
        settings.candidate_font_family = CandidateFontFamily::kSystem;
      }
    } else if (key == "candidate_color_scheme") {
      if (value == "blue") {
        settings.candidate_color_scheme = CandidateColorScheme::kBlue;
      } else if (value == "graphite") {
        settings.candidate_color_scheme = CandidateColorScheme::kGraphite;
      } else if (value == "system") {
        settings.candidate_color_scheme = CandidateColorScheme::kSystem;
      }
    } else if (key == "candidate_font_size") {
      std::size_t size = settings.candidate_font_size;
      const auto result = std::from_chars(value.data(), value.data() + value.size(), size);
      if (result.ec == std::errc{} && result.ptr == value.data() + value.size()) {
        settings.candidate_font_size =
            std::clamp(size, kMinimumCandidateFontSize, kMaximumCandidateFontSize);
      }
    } else if (key == "candidate_scale_with_text") {
      ParseBoolean(value, &settings.candidate_scale_with_text);
    }
  }
  return settings;
}

std::string SerializeSettings(const Settings& settings) {
  const char* layout = settings.candidate_layout == CandidateLayout::kHorizontal
                           ? "horizontal"
                           : "vertical";
  const char* switch_key = settings.input_mode_switch_key == InputModeSwitchKey::kControl
                               ? "control"
                               : "shift";
  const char* punctuation = settings.punctuation_style == PunctuationStyle::kHalfWidth
                                ? "half_width"
                                : "full_width";
  const char* page_keys = "comma_period";
  if (settings.page_key_set == PageKeySet::kSemicolonApostrophe) {
    page_keys = "semicolon_apostrophe";
  } else if (settings.page_key_set == PageKeySet::kBrackets) {
    page_keys = "brackets";
  }
  const char* character_set =
      settings.character_set == CharacterSet::kTraditional ? "traditional" : "simplified";
  const char* default_input_mode =
      settings.default_input_mode == DefaultInputMode::kEnglish ? "english" : "chinese";
  const char* theme_mode = "system";
  if (settings.theme_mode == ThemeMode::kLight) {
    theme_mode = "light";
  } else if (settings.theme_mode == ThemeMode::kDark) {
    theme_mode = "dark";
  }
  const char* candidate_page_mode =
      settings.candidate_page_mode == CandidatePageMode::kMultiLine ? "multi_line"
                                                                    : "single_line";
  const char* candidate_font_family = "source_han_sans";
  if (settings.candidate_font_family == CandidateFontFamily::kMicrosoftYaHei) {
    candidate_font_family = "microsoft_yahei";
  } else if (settings.candidate_font_family == CandidateFontFamily::kSystem) {
    candidate_font_family = "system";
  }
  const char* candidate_color_scheme = "system";
  if (settings.candidate_color_scheme == CandidateColorScheme::kBlue) {
    candidate_color_scheme = "blue";
  } else if (settings.candidate_color_scheme == CandidateColorScheme::kGraphite) {
    candidate_color_scheme = "graphite";
  }
  const std::size_t candidate_count = std::clamp(
      settings.candidate_count, kMinimumCandidateCount, kMaximumCandidateCount);

  return std::string("version=2\n") + "candidate_layout=" + layout + "\n" +
         "candidate_count=" + std::to_string(candidate_count) + "\n" +
         "input_mode_switch_key=" + switch_key + "\n" +
         "punctuation_style=" + punctuation + "\n" + "auto_pair_punctuation=" +
         (settings.auto_pair_punctuation ? "true" : "false") + "\n" + "page_keys=" +
         page_keys + "\n" +
         "character_set=" + character_set + "\n" + "default_input_mode=" +
         default_input_mode + "\n" + "initialism_spelling=" +
         (settings.initialism_spelling ? "true" : "false") + "\n" +
         "spelling_correction=" + (settings.spelling_correction ? "true" : "false") + "\n" +
         "correction_gn_ng=" + (settings.correction_gn_ng ? "true" : "false") + "\n" +
         "correction_mg_ng=" + (settings.correction_mg_ng ? "true" : "false") + "\n" +
         "correction_iou_iu=" + (settings.correction_iou_iu ? "true" : "false") + "\n" +
         "correction_uei_ui=" + (settings.correction_uei_ui ? "true" : "false") + "\n" +
         "correction_uen_un=" + (settings.correction_uen_un ? "true" : "false") + "\n" +
         "fuzzy_z_zh=" + (settings.fuzzy_z_zh ? "true" : "false") + "\n" +
         "fuzzy_c_ch=" + (settings.fuzzy_c_ch ? "true" : "false") + "\n" +
         "fuzzy_s_sh=" + (settings.fuzzy_s_sh ? "true" : "false") + "\n" +
         "fuzzy_l_n=" + (settings.fuzzy_l_n ? "true" : "false") + "\n" +
         "fuzzy_f_h=" + (settings.fuzzy_f_h ? "true" : "false") + "\n" +
         "fuzzy_r_l=" + (settings.fuzzy_r_l ? "true" : "false") + "\n" +
         "fuzzy_an_ang=" + (settings.fuzzy_an_ang ? "true" : "false") + "\n" +
         "fuzzy_en_eng=" + (settings.fuzzy_en_eng ? "true" : "false") + "\n" +
         "fuzzy_in_ing=" + (settings.fuzzy_in_ing ? "true" : "false") + "\n" +
         "fuzzy_ian_iang=" + (settings.fuzzy_ian_iang ? "true" : "false") + "\n" +
         "fuzzy_uan_uang=" + (settings.fuzzy_uan_uang ? "true" : "false") + "\n" +
         "smart_numeric_punctuation=" +
         (settings.smart_numeric_punctuation ? "true" : "false") + "\n" + "theme_mode=" +
         theme_mode + "\n" + "candidate_page_mode=" + candidate_page_mode + "\n" +
         "candidate_font_family=" + candidate_font_family + "\n" +
         "candidate_color_scheme=" + candidate_color_scheme + "\n" +
         "candidate_font_size=" +
         std::to_string(std::clamp(settings.candidate_font_size, kMinimumCandidateFontSize,
                                   kMaximumCandidateFontSize)) +
         "\n" + "candidate_scale_with_text=" +
         (settings.candidate_scale_with_text ? "true" : "false") + "\n";
}

CandidatePageSlice MakeCandidatePageSlice(std::size_t candidate_total, std::size_t page_size,
                                          std::size_t requested_offset) {
  page_size = std::clamp(page_size, kMinimumCandidateCount, kMaximumCandidateCount);
  if (candidate_total == 0) {
    return {};
  }
  const std::size_t maximum_offset = ((candidate_total - 1) / page_size) * page_size;
  const std::size_t offset = std::min((requested_offset / page_size) * page_size, maximum_offset);
  return CandidatePageSlice{offset, std::min(page_size, candidate_total - offset)};
}

}  // namespace ziliu::core
