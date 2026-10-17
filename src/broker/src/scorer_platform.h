#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace ziliu::broker::detail {

[[nodiscard]] std::string Sha256(std::string_view bytes);
[[nodiscard]] std::string ReadBoundedFile(const std::filesystem::path& path,
                                        std::size_t maximum_bytes);

// Only explicitly selected, digest-pinned local runtimes are accepted. The
// caller must independently enforce its runtime/version allowlist.
class RuntimeLibrary final {
 public:
  RuntimeLibrary(const std::filesystem::path& path, std::string_view expected_sha256);
  ~RuntimeLibrary();
  RuntimeLibrary(const RuntimeLibrary&) = delete;
  RuntimeLibrary& operator=(const RuntimeLibrary&) = delete;

  [[nodiscard]] void* Symbol(const char* name);
  [[nodiscard]] std::filesystem::path loaded_path() const;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

// Read-only admission checks. The launcher must already have imposed the
// process memory limit and CPU affinity. No ambient process/job state changes.
// Available physical RAM and process resident/working-set bytes are distinct
// from the enforced address-space (Linux) / process-commit (Windows) cap.
void CheckWorkerResources();

}  // namespace ziliu::broker::detail
