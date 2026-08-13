#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace ziliu::settings {

// Windows-only decoder. The implementation uses CNG and therefore the final
// executable must link bcrypt.lib.
struct SogouSsfEntry {
  std::string relative_path;
  std::vector<std::uint8_t> bytes;

  bool operator==(const SogouSsfEntry&) const = default;
};

struct SogouSsfDecodeResult {
  std::vector<SogouSsfEntry> entries;
  std::string error;

  [[nodiscard]] bool ok() const noexcept { return error.empty(); }
};

[[nodiscard]] SogouSsfDecodeResult DecodeSogouSsfV3(
    const std::filesystem::path& source_path);

// Returns a lowercase SHA-256 identity for the complete package. The identity
// is path-independent and lets converted metadata distinguish packages whose
// skin.ini files are identical but whose referenced images differ.
[[nodiscard]] std::optional<std::string> Sha256SogouSsfFile(
    const std::filesystem::path& source_path);

}  // namespace ziliu::settings
