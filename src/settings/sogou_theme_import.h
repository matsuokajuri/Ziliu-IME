#pragma once

#include "ziliu/core/theme_manifest.h"
#include <filesystem>
#include <optional>
#include <string>

namespace ziliu::settings {
struct SogouThemeImportResult {
  std::optional<core::ThemeManifest> manifest;
  std::wstring error;
  [[nodiscard]] bool ok() const { return manifest.has_value() && error.empty(); }
};

// Installs only a new content-addressed theme directory. Does not activate a
// theme or alter settings, fonts, registration, or the source SSF.
[[nodiscard]] SogouThemeImportResult InstallSogouSsfPackage(
    const std::filesystem::path& source, const std::filesystem::path& themes_directory);
}
