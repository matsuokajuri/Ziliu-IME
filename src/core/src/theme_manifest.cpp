#include "ziliu/core/theme_manifest.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace ziliu::core {
namespace {

constexpr std::uint32_t kMaximumInset = 4096;
constexpr std::uint32_t kMinimumFontSize = 8;
constexpr std::uint32_t kMaximumFontSize = 96;

bool IsValidUtf8(std::string_view text) {
  std::size_t index = 0;
  while (index < text.size()) {
    const auto first = static_cast<unsigned char>(text[index]);
    if (first <= 0x7FU) {
      ++index;
      continue;
    }

    std::size_t continuation_count = 0;
    std::uint32_t code_point = 0;
    std::uint32_t minimum = 0;
    if ((first & 0xE0U) == 0xC0U) {
      continuation_count = 1;
      code_point = first & 0x1FU;
      minimum = 0x80U;
    } else if ((first & 0xF0U) == 0xE0U) {
      continuation_count = 2;
      code_point = first & 0x0FU;
      minimum = 0x800U;
    } else if ((first & 0xF8U) == 0xF0U) {
      continuation_count = 3;
      code_point = first & 0x07U;
      minimum = 0x10000U;
    } else {
      return false;
    }
    if (index + continuation_count >= text.size()) {
      return false;
    }
    for (std::size_t offset = 1; offset <= continuation_count; ++offset) {
      const auto next = static_cast<unsigned char>(text[index + offset]);
      if ((next & 0xC0U) != 0x80U) {
        return false;
      }
      code_point = (code_point << 6U) | (next & 0x3FU);
    }
    if (code_point < minimum || code_point > 0x10FFFFU ||
        (code_point >= 0xD800U && code_point <= 0xDFFFU)) {
      return false;
    }
    index += continuation_count + 1;
  }
  return true;
}

bool HasControlCharacter(std::string_view text) {
  return std::any_of(text.begin(), text.end(), [](char value) {
    const auto byte = static_cast<unsigned char>(value);
    return byte < 0x20U || byte == 0x7FU;
  });
}

void AddIssue(std::vector<ThemeManifestIssue>* issues,
              ThemeManifestIssueCode code, std::string path,
              std::string message) {
  issues->push_back({code, std::move(path), std::move(message)});
}

void ValidateText(std::vector<ThemeManifestIssue>* issues,
                  std::string_view value, std::string path,
                  std::size_t maximum_bytes, bool required) {
  if (value.empty()) {
    if (required) {
      AddIssue(issues, ThemeManifestIssueCode::kMissingProperty,
               std::move(path), "value must not be empty");
    }
    return;
  }
  if (value.size() > maximum_bytes || !IsValidUtf8(value) ||
      HasControlCharacter(value)) {
    AddIssue(issues, ThemeManifestIssueCode::kInvalidProperty,
             std::move(path),
             "value must be bounded UTF-8 without control characters");
  }
}

bool IsValidThemeId(std::string_view id) {
  if (id.empty() || id.size() > 128U || id.front() == '.' ||
      id.back() == '.') {
    return false;
  }
  return std::all_of(id.begin(), id.end(), [](char value) {
    return (value >= 'a' && value <= 'z') ||
           (value >= '0' && value <= '9') || value == '.' || value == '_' ||
           value == '-';
  });
}

bool IsLowerSha256(std::string_view value) {
  return value.size() == 64U &&
         std::all_of(value.begin(), value.end(), [](char byte) {
           return (byte >= '0' && byte <= '9') ||
                  (byte >= 'a' && byte <= 'f');
         });
}

bool HasSupportedImageExtension(std::string_view path) {
  const std::size_t dot = path.find_last_of('.');
  if (dot == std::string_view::npos) {
    return false;
  }
  std::string extension(path.substr(dot));
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](char value) {
                   return static_cast<char>(std::tolower(
                       static_cast<unsigned char>(value)));
                 });
  return extension == ".png" || extension == ".apng";
}

void ValidateAsset(std::vector<ThemeManifestIssue>* issues,
                   std::string_view asset, std::string path, bool required) {
  if (asset.empty()) {
    if (required) {
      AddIssue(issues, ThemeManifestIssueCode::kMissingProperty,
               std::move(path), "asset path must not be empty");
    }
    return;
  }
  if (!IsSafeThemeAssetPath(asset)) {
    AddIssue(issues, ThemeManifestIssueCode::kUnsafeAssetPath,
             std::move(path), "asset must be a normalized relative path");
  } else if (!HasSupportedImageExtension(asset)) {
    AddIssue(issues, ThemeManifestIssueCode::kInvalidProperty,
             std::move(path), "normalized theme images must be PNG or APNG");
  }
}

void ValidateInsets(std::vector<ThemeManifestIssue>* issues,
                    const ThemeInsets& insets, std::string path) {
  if (insets.left > kMaximumInset || insets.top > kMaximumInset ||
      insets.right > kMaximumInset || insets.bottom > kMaximumInset) {
    AddIssue(issues, ThemeManifestIssueCode::kInvalidProperty,
             std::move(path), "each inset must be at most 4096");
  }
}

void ValidateLayout(std::vector<ThemeManifestIssue>* issues,
                    ThemeImageLayout layout, std::string path) {
  switch (layout) {
    case ThemeImageLayout::kStretch:
    case ThemeImageLayout::kTile:
    case ThemeImageLayout::kFixed:
      return;
  }
  AddIssue(issues, ThemeManifestIssueCode::kInvalidProperty, std::move(path),
           "image layout is not recognized");
}

void ValidateButton(std::vector<ThemeManifestIssue>* issues,
                    const ThemeButtonImages& button, std::string path) {
  ValidateAsset(issues, button.normal, path + ".normal", true);
  ValidateAsset(issues, button.hover, path + ".hover", false);
  ValidateAsset(issues, button.pressed, path + ".pressed", false);
}

void ValidateSurface(std::vector<ThemeManifestIssue>* issues,
                     const ThemeSurface& surface, std::string path) {
  constexpr auto kMaximumAnchor = static_cast<std::int32_t>(kMaximumInset);
  if (surface.anchor.has_value() &&
      (surface.anchor->x < -kMaximumAnchor ||
       surface.anchor->x > kMaximumAnchor ||
       surface.anchor->y < -kMaximumAnchor ||
       surface.anchor->y > kMaximumAnchor)) {
    AddIssue(issues, ThemeManifestIssueCode::kInvalidProperty,
             path + ".anchor", "anchor must be between -4096 and 4096");
  }
  if (surface.background.has_value()) {
    ValidateAsset(issues, surface.background->asset,
                  path + ".background.asset", true);
    ValidateInsets(issues, surface.background->stretch,
                   path + ".background.stretch");
    if (surface.background->horizontal_layout.has_value()) {
      ValidateLayout(issues, *surface.background->horizontal_layout,
                     path + ".background.horizontal_layout");
    }
    if (surface.background->vertical_layout.has_value()) {
      ValidateLayout(issues, *surface.background->vertical_layout,
                     path + ".background.vertical_layout");
    }
  }
  if (surface.overlays.size() > kMaximumThemeSurfaceOverlays) {
    AddIssue(issues, ThemeManifestIssueCode::kInvalidProperty,
             path + ".overlays", "surface has more than 128 overlays");
  }
  std::unordered_set<std::uint32_t> overlay_indices;
  for (std::size_t index = 0; index < surface.overlays.size(); ++index) {
    const ThemeOverlay& overlay = surface.overlays[index];
    const std::string overlay_path =
        path + ".overlays[" + std::to_string(index) + "]";
    ValidateAsset(issues, overlay.asset, overlay_path + ".asset", true);
    if (!overlay_indices.insert(overlay.custom_index).second) {
      AddIssue(issues, ThemeManifestIssueCode::kInvalidProperty,
               overlay_path + ".custom_index",
               "custom overlay indices must be unique");
    }
  }
  if (surface.preedit_insets.has_value()) {
    ValidateInsets(issues, *surface.preedit_insets,
                   path + ".preedit_insets");
  }
  if (surface.candidate_insets.has_value()) {
    ValidateInsets(issues, *surface.candidate_insets,
                   path + ".candidate_insets");
  }
  if (surface.separator.has_value()) {
    const ThemeSeparator& separator = *surface.separator;
    if (!separator.color.has_value()) {
      AddIssue(issues, ThemeManifestIssueCode::kMissingProperty,
               path + ".separator.color", "separator color is missing");
    }
    if (separator.left > kMaximumInset || separator.right > kMaximumInset ||
        separator.thickness == 0U ||
        separator.thickness > kMaximumInset) {
      AddIssue(issues, ThemeManifestIssueCode::kInvalidProperty,
               path + ".separator", "separator geometry is out of range");
    }
  }
  if (surface.previous_button.has_value()) {
    ValidateButton(issues, *surface.previous_button,
                   path + ".previous_button");
  }
  if (surface.next_button.has_value()) {
    ValidateButton(issues, *surface.next_button, path + ".next_button");
  }
}

void ValidateAppearance(std::vector<ThemeManifestIssue>* issues,
                        const ThemeAppearance& appearance) {
  ValidateText(issues, appearance.typography.chinese_font_family,
               "$.appearance.typography.chinese_font_family", 128U, false);
  ValidateText(issues, appearance.typography.english_font_family,
               "$.appearance.typography.english_font_family", 128U, false);
  if (appearance.typography.font_size.has_value() &&
      (*appearance.typography.font_size < kMinimumFontSize ||
       *appearance.typography.font_size > kMaximumFontSize)) {
    AddIssue(issues, ThemeManifestIssueCode::kInvalidProperty,
             "$.appearance.typography.font_size",
             "font size must be between 8 and 96");
  }
  if (appearance.typography.text_renderer.has_value()) {
    switch (*appearance.typography.text_renderer) {
      case ThemeTextRenderer::kSogouGdi:
      case ThemeTextRenderer::kSogouGdiPlus:
        break;
      default:
        AddIssue(issues, ThemeManifestIssueCode::kInvalidProperty,
                 "$.appearance.typography.text_renderer",
                 "text renderer is not recognized");
        break;
    }
  }
  if (!appearance.horizontal.has_value() &&
      !appearance.vertical.has_value()) {
    AddIssue(issues, ThemeManifestIssueCode::kMissingProperty,
             "$.appearance", "custom SSF must expose H1 or V1");
  }
  if (appearance.horizontal.has_value()) {
    ValidateSurface(issues, *appearance.horizontal,
                    "$.appearance.horizontal");
  }
  if (appearance.vertical.has_value()) {
    ValidateSurface(issues, *appearance.vertical, "$.appearance.vertical");
  }
}

}  // namespace

bool IsSafeThemeAssetPath(std::string_view path) {
  if (path.empty() || path.size() > 240U || path.front() == '/' ||
      path.front() == '\\' || path.back() == '/' ||
      path.find('\\') != std::string_view::npos ||
      path.find(':') != std::string_view::npos || !IsValidUtf8(path)) {
    return false;
  }
  std::size_t begin = 0;
  while (begin < path.size()) {
    const std::size_t end = path.find('/', begin);
    const std::size_t length = end == std::string_view::npos
                                   ? path.size() - begin
                                   : end - begin;
    const std::string_view component = path.substr(begin, length);
    if (component.empty() || component == "." || component == ".." ||
        HasControlCharacter(component)) {
      return false;
    }
    if (end == std::string_view::npos) {
      break;
    }
    begin = end + 1;
  }
  return true;
}

std::vector<ThemeManifestIssue> ValidateThemeManifest(
    const ThemeManifest& manifest) {
  std::vector<ThemeManifestIssue> issues;
  if (manifest.format_version != kThemeManifestVersion) {
    AddIssue(&issues, ThemeManifestIssueCode::kUnsupportedVersion,
             "$.format_version", "only manifest version 1 is supported");
  }
  if (!IsValidThemeId(manifest.id)) {
    AddIssue(&issues, ThemeManifestIssueCode::kInvalidProperty, "$.id",
             "id must be bounded lowercase ASCII");
  }
  ValidateText(&issues, manifest.name, "$.name", 128U, true);
  ValidateText(&issues, manifest.author, "$.author", 128U, false);
  ValidateText(&issues, manifest.version, "$.version", 64U, false);
  ValidateText(&issues, manifest.description, "$.description", 2048U,
               false);
  ValidateAsset(&issues, manifest.preview_asset, "$.preview_asset", false);
  if (manifest.source_format != "sogou-ssf") {
    AddIssue(&issues, ThemeManifestIssueCode::kInvalidProperty,
             "$.source_format", "source format must be sogou-ssf");
  }
  if (!IsLowerSha256(manifest.source_package_sha256)) {
    AddIssue(&issues, ThemeManifestIssueCode::kInvalidProperty,
             "$.source_package_sha256",
             "source package identity must be a lowercase SHA-256 digest");
  }
  if (manifest.base_dpi != 96U) {
    AddIssue(&issues, ThemeManifestIssueCode::kInvalidProperty, "$.base_dpi",
             "custom SSF coordinates use a 96 DPI base");
  }
  ValidateAppearance(&issues, manifest.appearance);
  return issues;
}

}  // namespace ziliu::core
