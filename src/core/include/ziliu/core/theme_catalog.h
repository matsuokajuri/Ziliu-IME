#pragma once

#include "ziliu/core/theme_manifest.h"

#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>

namespace ziliu::core {

struct InstalledTheme {
  ThemeManifest manifest;
  std::filesystem::path directory;

  bool operator==(const InstalledTheme&) const = default;
};

[[nodiscard]] std::vector<InstalledTheme> LoadInstalledThemes(
    const std::filesystem::path& themes_directory);
[[nodiscard]] std::optional<InstalledTheme> LoadInstalledTheme(
    const std::filesystem::path& themes_directory, std::string_view theme_id);

}  // namespace ziliu::core
