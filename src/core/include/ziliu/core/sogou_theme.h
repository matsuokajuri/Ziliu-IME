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
  std::string source_path;
  std::string target_path;

  bool operator==(const SogouThemeAsset&) const = default;
};

struct SogouThemeIssue {
  SogouThemeIssueCode code = SogouThemeIssueCode::kInvalidValue;
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

// Converts a decoded UTF-8 skin.ini into the custom-SSF model. Source images
// receive controlled PNG destinations; image decoding is a separate boundary.
[[nodiscard]] SogouThemeConversion ConvertSogouThemeIni(
    std::string_view utf8_ini, std::string_view source_hint,
    std::string_view source_package_sha256);

}  // namespace ziliu::core
