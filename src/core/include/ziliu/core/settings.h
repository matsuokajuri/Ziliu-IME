#pragma once

#include <cstddef>
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

struct Settings {
  CandidateLayout candidate_layout = CandidateLayout::kVertical;
  std::size_t candidate_count = 5;
  InputModeSwitchKey input_mode_switch_key = InputModeSwitchKey::kShift;
  PunctuationStyle punctuation_style = PunctuationStyle::kFullWidth;
  PageKeySet page_key_set = PageKeySet::kCommaPeriod;
  CharacterSet character_set = CharacterSet::kSimplified;

  bool operator==(const Settings&) const = default;
};

struct CandidatePageSlice {
  std::size_t offset = 0;
  std::size_t count = 0;

  bool operator==(const CandidatePageSlice&) const = default;
};

inline constexpr std::size_t kMinimumCandidateCount = 3;
inline constexpr std::size_t kMaximumCandidateCount = 9;

[[nodiscard]] Settings ParseSettings(std::string_view text);
[[nodiscard]] std::string SerializeSettings(const Settings& settings);
[[nodiscard]] CandidatePageSlice MakeCandidatePageSlice(std::size_t candidate_total,
                                                        std::size_t page_size,
                                                        std::size_t requested_offset);

}  // namespace ziliu::core
