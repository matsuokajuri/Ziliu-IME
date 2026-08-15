#pragma once

#include "ziliu/core/theme_manifest.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ziliu::core {

inline constexpr std::size_t kMaximumSogouThemeIniBytes = 256 * 1024;

enum class SogouThemeIssueCode {
  kIniTooLarge,
  kInvalidUtf8,
  kInvalidEncoding,
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

enum class SogouThemeIniEncoding {
  kUnknown,
  kUtf8,
  kUtf8Bom,
  kUtf16LeBom,
};

struct SogouThemeTextNormalization {
  SogouThemeIniEncoding encoding = SogouThemeIniEncoding::kUnknown;
  std::string utf8;
  std::string error;

  [[nodiscard]] bool ok() const noexcept { return error.empty(); }
};

struct SogouThemePackageEntryView {
  std::string_view relative_path;
  std::span<const std::uint8_t> bytes;
};

struct SogouThemePackageConversion {
  SogouThemeIniEncoding skin_ini_encoding =
      SogouThemeIniEncoding::kUnknown;
  SogouThemeConversion conversion;

  [[nodiscard]] bool ok() const noexcept { return conversion.ok(); }
};

struct SogouThemeResolvedAsset {
  std::string source_path;
  std::string target_path;
  std::vector<std::uint8_t> bytes;

  bool operator==(const SogouThemeResolvedAsset&) const = default;
};

struct SogouThemeResourceBinding {
  std::string source_package_sha256;
  std::vector<SogouThemeResolvedAsset> assets;
  std::string error;

  [[nodiscard]] bool ok() const noexcept { return error.empty(); }
};

// Converts raw skin.ini bytes to strict UTF-8 without changing text semantics.
// Accepted encodings are UTF-8 (with or without BOM) and BOM-marked UTF-16LE.
[[nodiscard]] SogouThemeTextNormalization NormalizeSogouThemeIniText(
    std::span<const std::uint8_t> bytes);

// Converts a decoded UTF-8 skin.ini into the custom-SSF model. Source images
// receive controlled PNG destinations; image decoding is a separate boundary.
[[nodiscard]] SogouThemeConversion ConvertSogouThemeIni(
    std::string_view utf8_ini, std::string_view source_hint,
    std::string_view source_package_sha256);

// Locates the unique root skin.ini in decoded package entries, normalizes its
// text, and delegates all SSF semantics to ConvertSogouThemeIni.
[[nodiscard]] SogouThemePackageConversion ConvertSogouThemePackage(
    std::span<const SogouThemePackageEntryView> entries,
    std::string_view source_hint, std::string_view source_package_sha256);

// Resolves every manifest source asset to exactly one decoded package entry.
// Returned bytes are owned so that they remain valid after decoder storage dies.
[[nodiscard]] SogouThemeResourceBinding ResolveSogouThemePackageResources(
    const SogouThemePackageConversion& package,
    std::span<const SogouThemePackageEntryView> entries);

}  // namespace ziliu::core
