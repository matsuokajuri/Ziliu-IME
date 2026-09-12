#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ziliu::core {

inline constexpr std::uint32_t kThemeManifestVersion = 1;
inline constexpr std::size_t kMaximumThemeManifestBytes = 256 * 1024;
inline constexpr std::size_t kMaximumThemePackageEntries = 128;
inline constexpr std::size_t kMaximumThemePackageBytes = 32 * 1024 * 1024;
inline constexpr std::size_t kMaximumThemeAssetBytes = 8 * 1024 * 1024;
inline constexpr std::uint32_t kMaximumThemeImageDimension = 4096;
inline constexpr std::size_t kMaximumThemeDecodedPixels = 64 * 1024 * 1024;
inline constexpr std::size_t kMaximumThemeSurfaceOverlays = 128;
inline constexpr std::string_view kThemeManifestFileName = "manifest.json";
inline constexpr std::string_view kThemePackageExtension = ".zlt";
inline constexpr std::string_view kDefaultThemeId = "org.ziliu.default";

enum class ThemeImageLayout {
  kStretch,
  kTile,
  kFixed,
};

enum class ThemeManifestIssueCode {
  kInvalidJson,
  kUnsupportedVersion,
  kMissingProperty,
  kInvalidProperty,
  kUnsafeAssetPath,
  kManifestTooLarge,
};

struct ThemeInsets {
  std::uint32_t left = 0;
  std::uint32_t top = 0;
  std::uint32_t right = 0;
  std::uint32_t bottom = 0;

  bool operator==(const ThemeInsets&) const = default;
};

struct ThemeImage {
  std::string asset;
  ThemeInsets stretch;
  ThemeImageLayout horizontal_layout = ThemeImageLayout::kStretch;
  ThemeImageLayout vertical_layout = ThemeImageLayout::kStretch;

  bool operator==(const ThemeImage&) const = default;
};

struct ThemeSeparator {
  std::optional<std::uint32_t> color;
  std::string asset;
  std::uint32_t left = 0;
  std::uint32_t right = 0;
  std::uint32_t thickness = 1;

  bool operator==(const ThemeSeparator&) const = default;
};

struct ThemeButtonImages {
  std::string normal;
  std::string hover;
  std::string pressed;

  bool operator==(const ThemeButtonImages&) const = default;
};

struct ThemeOverlay {
  std::string asset;
  // Original customN index from an imported same-window theme.
  std::uint32_t custom_index = 0;
  // Lower values are painted first. Sogou imports preserve customN order here.
  std::uint32_t draw_order = 0;
  // The ten customN_align integers are intentionally opaque and lossless in v1.
  std::array<std::int32_t, 10> align{};

  bool operator==(const ThemeOverlay&) const = default;
};

struct ThemeSurface {
  std::optional<ThemeImage> background;
  std::vector<ThemeOverlay> overlays;
  std::optional<ThemeInsets> preedit_insets;
  std::optional<ThemeInsets> candidate_insets;
  std::optional<ThemeSeparator> separator;
  std::optional<ThemeButtonImages> previous_button;
  std::optional<ThemeButtonImages> next_button;
  std::optional<ThemeButtonImages> expand_button;
  std::optional<ThemeButtonImages> collapse_button;
  std::optional<ThemeButtonImages> menu_button;

  bool operator==(const ThemeSurface&) const = default;
};

struct ThemePalette {
  // Colors use 0xAARRGGBB in memory and #RRGGBB or #RRGGBBAA in the manifest.
  std::uint32_t preedit_text = 0xFF202124;
  std::uint32_t candidate_text = 0xFF202124;
  std::uint32_t highlighted_candidate_text = 0xFF0067C0;
  std::uint32_t background = 0xFFFAFAFA;
  std::uint32_t highlighted_background = 0xFFE1EFFF;
  std::uint32_t muted_text = 0xFF6B7280;
  std::uint32_t separator = 0xFFD1D5DB;

  bool operator==(const ThemePalette&) const = default;
};

struct ThemeTypography {
  std::string chinese_font_family = "Source Han Sans SC";
  std::string english_font_family = "Segoe UI Variable Text";
  std::uint32_t font_size = 17;

  bool operator==(const ThemeTypography&) const = default;
};

struct ThemeAppearance {
  ThemePalette palette;
  ThemeTypography typography;
  ThemeSurface horizontal;
  ThemeSurface vertical;

  bool operator==(const ThemeAppearance&) const = default;
};

struct ThemeManifest {
  std::uint32_t format_version = kThemeManifestVersion;
  std::string id;
  std::string name;
  std::string author;
  std::string version;
  std::string license;
  std::string description;
  std::string homepage;
  std::string preview_asset;
  std::string source_format = "zlt";
  std::uint32_t base_dpi = 96;
  ThemeAppearance light;
  std::optional<ThemeAppearance> dark;

  bool operator==(const ThemeManifest&) const = default;
};

struct ThemeManifestIssue {
  ThemeManifestIssueCode code = ThemeManifestIssueCode::kInvalidProperty;
  std::size_t offset = 0;
  std::string path;
  std::string message;

  bool operator==(const ThemeManifestIssue&) const = default;
};

struct ThemeManifestParseResult {
  ThemeManifest manifest;
  std::vector<ThemeManifestIssue> issues;

  [[nodiscard]] bool ok() const noexcept { return issues.empty(); }
};

[[nodiscard]] ThemeManifest MakeDefaultThemeManifest();
[[nodiscard]] bool IsReservedThemeId(std::string_view id) noexcept;
[[nodiscard]] bool IsSafeThemeAssetPath(std::string_view path);
[[nodiscard]] std::vector<ThemeManifestIssue> ValidateThemeManifest(
    const ThemeManifest& manifest);
[[nodiscard]] ThemeManifestParseResult ParseThemeManifest(std::string_view json);
[[nodiscard]] std::string SerializeThemeManifest(const ThemeManifest& manifest);

}  // namespace ziliu::core
