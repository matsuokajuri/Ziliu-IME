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

void ParseColor(std::string_view value, std::uint32_t* destination) {
  if (!value.empty() && value.front() == '#') {
    value.remove_prefix(1);
  }
  std::uint32_t color = 0;
  const auto result = std::from_chars(value.data(), value.data() + value.size(), color, 16);
  if (value.size() == 6 && result.ec == std::errc{} &&
      result.ptr == value.data() + value.size()) {
    *destination = color;
  }
}

std::string SerializeColor(std::uint32_t color) {
  constexpr char digits[] = "0123456789ABCDEF";
  std::string result(7, '0');
  result[0] = '#';
  color &= 0xFFFFFF;
  for (std::size_t index = 0; index < 6; ++index) {
    const std::size_t shift = (5 - index) * 4;
    result[index + 1] = digits[(color >> shift) & 0xF];
  }
  return result;
}

void ParseFontFamily(std::string_view value, std::string* destination) {
  if (value.empty()) {
    return;
  }
  if (value == "source_han_sans") {
    *destination = "Source Han Sans SC";
  } else if (value == "microsoft_yahei") {
    *destination = "Microsoft YaHei UI";
  } else if (value == "simsun") {
    *destination = "SimSun";
  } else if (value == "segoe_ui") {
    *destination = "Segoe UI Variable Text";
  } else if (value == "arial") {
    *destination = "Arial";
  } else {
    *destination = value;
  }
}

std::uint32_t BlendColor(std::uint32_t background, std::uint32_t foreground, float amount) {
  const auto blend_channel = [amount](std::uint32_t from, std::uint32_t to) {
    return static_cast<std::uint32_t>(
        std::clamp(static_cast<float>(from) +
                       (static_cast<float>(to) - static_cast<float>(from)) * amount,
                   0.0F, 255.0F));
  };
  const std::uint32_t red =
      blend_channel((background >> 16) & 0xFF, (foreground >> 16) & 0xFF);
  const std::uint32_t green =
      blend_channel((background >> 8) & 0xFF, (foreground >> 8) & 0xFF);
  const std::uint32_t blue = blend_channel(background & 0xFF, foreground & 0xFF);
  return (red << 16) | (green << 8) | blue;
}

bool IsDarkColor(std::uint32_t color) {
  const std::uint32_t red = (color >> 16) & 0xFF;
  const std::uint32_t green = (color >> 8) & 0xFF;
  const std::uint32_t blue = color & 0xFF;
  return red * 299 + green * 587 + blue * 114 < 128000;
}

}  // namespace

CandidatePalette ResolveCandidatePalette(const Settings& settings, bool dark_theme) {
  CandidatePalette palette;
  palette.preedit_color = dark_theme ? 0xF5F6F7 : 0x202124;
  palette.highlighted_candidate_color = dark_theme ? 0x75B6E7 : 0x0067C0;
  palette.candidate_text_color = dark_theme ? 0xF5F6F7 : 0x202124;
  palette.candidate_background_color = dark_theme ? 0x202124 : 0xFAFAFA;

  const bool custom_background_matches_theme =
      IsDarkColor(settings.candidate_background_color) == dark_theme;
  if (settings.custom_candidate_colors && custom_background_matches_theme) {
    palette.preedit_color = settings.preedit_color;
    palette.highlighted_candidate_color = settings.highlighted_candidate_color;
    palette.candidate_text_color = settings.candidate_text_color;
    palette.candidate_background_color = settings.candidate_background_color;
  }

  palette.muted_color =
      BlendColor(palette.candidate_text_color, palette.candidate_background_color, 0.52F);
  palette.highlight_background_color =
      BlendColor(palette.candidate_background_color, palette.highlighted_candidate_color, 0.14F);
  return palette;
}

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
    } else if (key == "chinese_candidates_only") {
      ParseBoolean(value, &settings.chinese_candidates_only);
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
    } else if (key == "custom_candidate_colors") {
      ParseBoolean(value, &settings.custom_candidate_colors);
    } else if (key == "preedit_color") {
      ParseColor(value, &settings.preedit_color);
    } else if (key == "highlighted_candidate_color") {
      ParseColor(value, &settings.highlighted_candidate_color);
    } else if (key == "candidate_text_color") {
      ParseColor(value, &settings.candidate_text_color);
    } else if (key == "candidate_background_color") {
      ParseColor(value, &settings.candidate_background_color);
    } else if (key == "custom_candidate_fonts") {
      ParseBoolean(value, &settings.custom_candidate_fonts);
    } else if (key == "candidate_chinese_font_family") {
      ParseFontFamily(value, &settings.candidate_chinese_font_family);
    } else if (key == "candidate_english_font_family") {
      ParseFontFamily(value, &settings.candidate_english_font_family);
    } else if (key == "candidate_font_family") {
      settings.custom_candidate_fonts = value != "system";
      if (value == "microsoft_yahei") {
        settings.candidate_chinese_font_family = "Microsoft YaHei UI";
      }
    } else if (key == "candidate_color_scheme") {
      settings.custom_candidate_colors = value != "system";
      if (value == "blue") {
        settings.highlighted_candidate_color = 0x0067C0;
        settings.candidate_background_color = 0xF7FAFF;
      } else if (value == "graphite") {
        settings.highlighted_candidate_color = 0x555555;
        settings.candidate_background_color = 0xF4F4F4;
      }
    } else if (key == "custom_candidate_font_size") {
      ParseBoolean(value, &settings.custom_candidate_font_size);
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
  const std::size_t candidate_count = std::clamp(
      settings.candidate_count, kMinimumCandidateCount, kMaximumCandidateCount);

  return std::string("version=5\n") + "candidate_layout=" + layout + "\n" +
         "candidate_count=" + std::to_string(candidate_count) + "\n" +
         "input_mode_switch_key=" + switch_key + "\n" +
         "punctuation_style=" + punctuation + "\n" + "auto_pair_punctuation=" +
         (settings.auto_pair_punctuation ? "true" : "false") + "\n" + "page_keys=" +
         page_keys + "\n" +
         "character_set=" + character_set + "\n" + "default_input_mode=" +
         default_input_mode + "\n" + "chinese_candidates_only=" +
         (settings.chinese_candidates_only ? "true" : "false") + "\n" +
         "initialism_spelling=" +
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
         "custom_candidate_colors=" +
         (settings.custom_candidate_colors ? "true" : "false") + "\n" + "preedit_color=" +
         SerializeColor(settings.preedit_color) + "\n" + "highlighted_candidate_color=" +
         SerializeColor(settings.highlighted_candidate_color) + "\n" +
         "candidate_text_color=" + SerializeColor(settings.candidate_text_color) + "\n" +
         "candidate_background_color=" + SerializeColor(settings.candidate_background_color) +
         "\n" + "custom_candidate_fonts=" +
         (settings.custom_candidate_fonts ? "true" : "false") + "\n" +
         "candidate_chinese_font_family=" + settings.candidate_chinese_font_family + "\n" +
         "candidate_english_font_family=" + settings.candidate_english_font_family + "\n" +
         "custom_candidate_font_size=" +
         (settings.custom_candidate_font_size ? "true" : "false") + "\n" +
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
