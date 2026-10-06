#pragma once

#include <algorithm>
#include <filesystem>
#include <span>
#include <string_view>
#include <system_error>

namespace ziliu::broker::detail {

// Initial provisioning only. Existing regular overlays belong to the user;
// updating bundled defaults must not silently replace their customization.
[[nodiscard]] inline bool EnsureInitialOverlay(
    const std::filesystem::path& source, const std::filesystem::path& destination) {
  std::error_code status_error;
  const auto status = std::filesystem::symlink_status(destination, status_error);
  if (status.type() != std::filesystem::file_type::not_found) {
    return !status_error && std::filesystem::is_regular_file(status);
  }
  if (status_error && status_error != std::errc::no_such_file_or_directory) return false;
  std::error_code copy_error;
  return std::filesystem::copy_file(source, destination,
                                    std::filesystem::copy_options::none,
                                    copy_error) && !copy_error;
}

// A truncated API buffer cannot certify the active schema. Only a complete,
// exact name lets the caller skip selecting and reopening that schema.
[[nodiscard]] inline bool CompleteSchemaMatches(
    std::span<const char> schema, std::string_view requested) noexcept {
  const auto terminator = std::find(schema.begin(), schema.end(), '\0');
  return terminator != schema.end() &&
      std::string_view(schema.data(), static_cast<std::size_t>(terminator - schema.begin())) == requested;
}

}  // namespace ziliu::broker::detail
