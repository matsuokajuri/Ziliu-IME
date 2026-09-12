#pragma once

#include <cstdint>
#include <filesystem>
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

}  // namespace ziliu::settings
