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
  const std::size_t candidate_count = std::clamp(
      settings.candidate_count, kMinimumCandidateCount, kMaximumCandidateCount);

  return std::string("version=1\n") + "candidate_layout=" + layout + "\n" +
         "candidate_count=" + std::to_string(candidate_count) + "\n" +
         "input_mode_switch_key=" + switch_key + "\n" +
         "punctuation_style=" + punctuation + "\n" + "auto_pair_punctuation=" +
         (settings.auto_pair_punctuation ? "true" : "false") + "\n" + "page_keys=" +
         page_keys + "\n" +
         "character_set=" + character_set + "\n";
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
