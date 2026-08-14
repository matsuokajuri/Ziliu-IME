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
inline constexpr std::size_t kMaximumThemeSurfaceOverlays = 128;

enum class ThemeImageLayout {
  kStretch,
  kTile,
  kFixed,
};

enum class ThemeTextRenderer {
  kSogouGdi,
  kSogouGdiPlus,
};

enum class ThemeManifestIssueCode {
  kUnsupportedVersion,
  kMissingProperty,
  kInvalidProperty,
  kUnsafeAssetPath,
};

struct ThemeInsets {
  std::uint32_t left = 0;
  std::uint32_t top = 0;
  std::uint32_t right = 0;
  std::uint32_t bottom = 0;

  bool operator==(const ThemeInsets&) const = default;
};

struct ThemePoint {
  std::int32_t x = 0;
  std::int32_t y = 0;

  bool operator==(const ThemePoint&) const = default;
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
  std::uint32_t custom_index = 0;
  std::uint32_t draw_order = 0;
  // SSF customN_align is preserved losslessly until its rendering semantics
  // are established from a custom-skin reference.
  std::array<std::int32_t, 10> raw_alignment{};

  bool operator==(const ThemeOverlay&) const = default;
};

struct ThemeSurface {
  ThemePoint anchor;
  std::optional<ThemeImage> background;
  std::vector<ThemeOverlay> overlays;
  std::optional<ThemeInsets> preedit_insets;
  std::optional<ThemeInsets> candidate_insets;
  std::optional<ThemeSeparator> separator;
  std::optional<ThemeButtonImages> previous_button;
  std::optional<ThemeButtonImages> next_button;

  bool operator==(const ThemeSurface&) const = default;
};

struct ThemePalette {
  std::optional<std::uint32_t> preedit_text;
  std::optional<std::uint32_t> candidate_text;
  std::optional<std::uint32_t> highlighted_candidate_text;
  std::optional<std::uint32_t> annotation_text;
  std::optional<std::uint32_t> caret_text;
  std::optional<std::uint32_t> highlighted_background;

  bool operator==(const ThemePalette&) const = default;
};

struct ThemeTypography {
  std::string chinese_font_family;
  std::string english_font_family;
  std::optional<std::uint32_t> font_size;
  std::optional<ThemeTextRenderer> text_renderer;

  bool operator==(const ThemeTypography&) const = default;
};

struct ThemeAppearance {
  ThemePalette palette;
  ThemeTypography typography;
  std::optional<ThemeSurface> horizontal;
  std::optional<ThemeSurface> vertical;

  bool operator==(const ThemeAppearance&) const = default;
};

struct ThemeManifest {
  std::uint32_t format_version = kThemeManifestVersion;
  std::string id;
  std::string name;
  std::string author;
  std::string version;
  std::string description;
  std::string preview_asset;
  std::string source_format = "sogou-ssf";
  std::string source_package_sha256;
  std::uint32_t base_dpi = 96;
  ThemeAppearance appearance;

  bool operator==(const ThemeManifest&) const = default;
};

struct ThemeManifestIssue {
  ThemeManifestIssueCode code = ThemeManifestIssueCode::kInvalidProperty;
  std::string path;
  std::string message;

  bool operator==(const ThemeManifestIssue&) const = default;
};

[[nodiscard]] bool IsSafeThemeAssetPath(std::string_view path);
[[nodiscard]] std::vector<ThemeManifestIssue> ValidateThemeManifest(
    const ThemeManifest& manifest);

}  // namespace ziliu::core
