#include "ziliu/broker/rime_initialization.h"

#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace {
unsigned checks = 0;
void Expect(bool value, const char* message) {
  ++checks;
  if (!value) { std::cerr << "FAILED: " << message << '\n'; std::exit(1); }
}
void Write(const std::filesystem::path& file, const std::string& value) {
  std::ofstream stream(file, std::ios::binary);
  stream << value;
  Expect(stream.good(), "fixture write");
}
std::string Read(const std::filesystem::path& file) {
  std::ifstream stream(file, std::ios::binary);
  Expect(stream.good(), "fixture read");
  return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}
void TestOverlay(const std::filesystem::path& root) {
  using ziliu::broker::detail::EnsureInitialOverlay;
  const auto source = root / "bundled.yaml";
  const auto destination = root / "user.yaml";
  Write(source, "bundled default\n");
  Expect(EnsureInitialOverlay(source, destination), "missing overlay is created");
  Expect(Read(destination) == "bundled default\n", "initial content copied");
  Write(destination, "synthetic user customization\n");
  const auto earlier = std::filesystem::file_time_type::clock::now() - std::chrono::hours(24);
  std::filesystem::last_write_time(destination, earlier);
  const auto unchanged_time = std::filesystem::last_write_time(destination);
  Write(source, "new bundled default\n");
  Expect(EnsureInitialOverlay(source, destination), "existing user overlay remains accepted");
  Expect(Read(destination) == "synthetic user customization\n", "custom content survives startup");
  Expect(std::filesystem::last_write_time(destination) == unchanged_time,
         "existing overlay timestamp remains unchanged");
  std::filesystem::remove(source);
  Expect(EnsureInitialOverlay(source, destination), "existing overlay does not require a recopy");
  Expect(!EnsureInitialOverlay(source, root / "missing.yaml"), "missing source fails initial provisioning");
  const auto directory = root / "not-a-file";
  std::filesystem::create_directory(directory);
  Expect(!EnsureInitialOverlay(destination, directory), "directory is not a valid overlay");
  Expect(!EnsureInitialOverlay(destination, root / "missing-parent" / "user.yaml"),
         "invalid destination fails without creating parents");
  std::error_code link_error;
  const auto link = root / "linked.yaml";
  std::filesystem::create_symlink(destination, link, link_error);
  if (!link_error) {
    Expect(!EnsureInitialOverlay(destination, link), "symbolic-link overlay is rejected");
    Expect(Read(destination) == "synthetic user customization\n", "linked target is unchanged");
  } else {
    std::cout << "Symbolic-link check unavailable in this environment\n";
  }
}
void TestSchema() {
  using ziliu::broker::detail::CompleteSchemaMatches;
  const char ordinary[] = "rime_ice";
  const char restricted[] = "ziliu_private";
  Expect(CompleteSchemaMatches(ordinary, "rime_ice"), "exact ordinary schema matches");
  Expect(CompleteSchemaMatches(restricted, "ziliu_private"), "exact restricted schema matches");
  Expect(!CompleteSchemaMatches(ordinary, "ziliu_private"), "different schema requires selection");
  Expect(!CompleteSchemaMatches(ordinary, "rime"), "prefix is not an exact match");
  Expect(!CompleteSchemaMatches(ordinary, "RIME_ICE"), "schema match is case sensitive");
  Expect(!CompleteSchemaMatches({}, "rime_ice"), "absent buffer cannot certify schema");
  const std::array<char, 8> truncated{'r','i','m','e','_','i','c','e'};
  Expect(!CompleteSchemaMatches(truncated, "rime_ice"), "missing terminator requires selection");
  const char suffixed[] = "rime_ice_other";
  Expect(!CompleteSchemaMatches(suffixed, "rime_ice"), "different complete suffix requires selection");
}
}

int main() {
  try {
    // Work only in a new child of the CTest working directory, never a real
    // Rime user profile. Each run owns its fixture and refuses name collisions.
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::current_path() / ("ziliu-overlay-test-" + std::to_string(nonce));
    Expect(std::filesystem::create_directory(root), "new owned fixture directory");
    struct Cleanup { std::filesystem::path path; ~Cleanup() {
      std::error_code error; std::filesystem::remove_all(path, error);
    } } cleanup{root};
    TestOverlay(root);
    TestSchema();
    std::cout << checks << " Rime startup helper checks passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "Fixture failed: " << error.what() << '\n';
    return 1;
  }
}
