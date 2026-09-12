#pragma once

#include "ziliu/core/theme_manifest.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace ziliu::core {

inline constexpr std::size_t kMaximumSogouThemeIniBytes = 256 * 1024;

enum class SogouThemeIssueCode {
  kIniTooLarge,
  kInvalidUtf8,
  kMalformedIni,
  kDuplicateProperty,
  kMissingProperty,
  kInvalidValue,
  kUnsafeAssetPath,
  kInvalidManifest,
};

struct SogouThemeAsset {
  // The normalized relative path inside the SSF package.
  std::string source_path;
  // The controlled PNG path referenced by the generated ZLT manifest.
  std::string target_path;

  bool operator==(const SogouThemeAsset&) const = default;
};

struct SogouThemeIssue {
  SogouThemeIssueCode code = SogouThemeIssueCode::kInvalidValue;
  // One-based skin.ini line number. Zero denotes a document or manifest issue.
  std::size_t line = 0;
  std::string path;
  std::string message;

  bool operator==(const SogouThemeIssue&) const = default;
};

struct SogouThemeConversion {
  ThemeManifest manifest;
  std::vector<SogouThemeAsset> assets;
  std::vector<SogouThemeIssue> issues;

  [[nodiscard]] bool ok() const noexcept { return issues.empty(); }
};

// Converts an already decoded UTF-8 skin.ini document into the platform-independent
// ZLT manifest model. Referenced images are assigned controlled PNG destinations;
// decoding and writing those images remains the responsibility of the settings app.
[[nodiscard]] SogouThemeConversion ConvertSogouThemeIni(
    std::string_view utf8_ini, std::string_view source_hint);

}  // namespace ziliu::core
