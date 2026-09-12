#include "ziliu/core/theme_catalog.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>

namespace ziliu::core {
namespace {

std::optional<InstalledTheme> LoadThemeDirectory(const std::filesystem::path& directory) {
  std::error_code status_error;
  const auto status = std::filesystem::symlink_status(directory, status_error);
  if (status_error || !std::filesystem::is_directory(status) ||
      std::filesystem::is_symlink(status)) {
    return std::nullopt;
  }

  const std::filesystem::path manifest_path =
      directory / std::filesystem::path(kThemeManifestFileName);
  const auto manifest_status = std::filesystem::symlink_status(manifest_path, status_error);
  if (status_error || !std::filesystem::is_regular_file(manifest_status) ||
      std::filesystem::is_symlink(manifest_status)) {
    return std::nullopt;
  }
  const std::uintmax_t manifest_size = std::filesystem::file_size(manifest_path, status_error);
  if (status_error || manifest_size > kMaximumThemeManifestBytes) {
    return std::nullopt;
  }

  std::ifstream stream(manifest_path, std::ios::binary);
  if (!stream) {
    return std::nullopt;
  }
  const std::string contents{std::istreambuf_iterator<char>(stream),
                             std::istreambuf_iterator<char>()};
  const ThemeManifestParseResult parsed = ParseThemeManifest(contents);
  if (!parsed.ok() || parsed.manifest.id != directory.filename().string()) {
    return std::nullopt;
  }
  return InstalledTheme{parsed.manifest, directory};
}

bool IsValidThemeId(std::string_view theme_id) {
  if (theme_id.empty() || theme_id.size() > 128) {
    return false;
  }
  const auto is_lower = [](char character) {
    return character >= 'a' && character <= 'z';
  };
  const auto is_digit = [](char character) {
    return character >= '0' && character <= '9';
  };
  if (!is_lower(theme_id.front()) && !is_digit(theme_id.front())) {
    return false;
  }
  return std::all_of(theme_id.begin(), theme_id.end(), [&](char character) {
    return is_lower(character) || is_digit(character) || character == '.' ||
           character == '_' || character == '-';
  });
}

}  // namespace

std::vector<InstalledTheme> LoadInstalledThemes(
    const std::filesystem::path& themes_directory) {
  std::vector<InstalledTheme> themes;
  std::error_code iterator_error;
  std::filesystem::directory_iterator iterator(
      themes_directory, std::filesystem::directory_options::skip_permission_denied,
      iterator_error);
  if (iterator_error) {
    return themes;
  }
  for (const auto& entry : iterator) {
    if (auto theme = LoadThemeDirectory(entry.path()); theme.has_value()) {
      themes.push_back(std::move(*theme));
    }
  }
  std::sort(themes.begin(), themes.end(), [](const InstalledTheme& left,
                                             const InstalledTheme& right) {
    return left.manifest.id < right.manifest.id;
  });
  return themes;
}

std::optional<InstalledTheme> LoadInstalledTheme(
    const std::filesystem::path& themes_directory, std::string_view theme_id) {
  if (!IsValidThemeId(theme_id)) {
    return std::nullopt;
  }
  return LoadThemeDirectory(themes_directory / std::filesystem::path(theme_id));
}

}  // namespace ziliu::core
