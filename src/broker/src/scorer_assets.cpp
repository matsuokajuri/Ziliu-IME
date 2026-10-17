#include "scorer_assets.h"

#include "onnx_graph_gate.h"
#include "scorer_platform.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <stdexcept>
#include <string_view>

namespace ziliu::broker::detail {
namespace {

void Require(bool condition, const char* reason) {
  if (!condition) throw std::runtime_error(reason);
}

bool IsDigest(std::string_view value) {
  return value.size() == 64 && std::all_of(value.begin(), value.end(), [](char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
  });
}

bool SafeFilename(std::string_view name) {
  return !name.empty() && name.size() <= 128 && name != "." && name != ".." &&
         name.front() != '.' && name.find("..") == std::string_view::npos &&
         std::all_of(name.begin(), name.end(), [](char c) {
           return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
         });
}

std::string_view TakeLine(std::string_view& remaining) {
  const auto end = remaining.find('\n');
  Require(end != std::string_view::npos, "manifest or vocabulary requires LF lines");
  const auto line = remaining.substr(0, end);
  remaining.remove_prefix(end + 1);
  return line;
}

std::uint32_t Unsigned(std::string_view value) {
  Require(!value.empty() && value.size() <= 7 && (value.size() == 1 || value.front() != '0'),
          "invalid vocabulary number");
  std::uint32_t result = 0;
  const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
  Require(parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size(),
          "invalid vocabulary number");
  return result;
}

core::scoring::Vocabulary ParseVocabulary(std::string_view bytes) {
  Require(TakeLine(bytes) == "ziliu-scalar-vocabulary-v1", "unsupported vocabulary format");
  core::scoring::Vocabulary vocabulary;
  std::uint32_t previous = 0;
  while (!bytes.empty()) {
    Require(vocabulary.size() < 8189, "vocabulary count");
    const auto line = TakeLine(bytes);
    const auto tab = line.find('\t');
    Require(tab != std::string_view::npos, "invalid vocabulary entry");
    const auto scalar = Unsigned(line.substr(0, tab));
    const auto token = Unsigned(line.substr(tab + 1));
    Require(scalar > previous, "vocabulary order or duplicate");
    previous = scalar;
    vocabulary.emplace(scalar, static_cast<std::int64_t>(token));
  }
  Require(vocabulary.size() == 8189, "incomplete vocabulary");
  core::scoring::ValidateVocabulary(vocabulary);
  return vocabulary;
}

}  // namespace

VerifiedScorerAssets LoadScorerAssets(const TrustedScorerAssets& trusted) {
  Require(trusted.manifest_path.is_absolute() && IsDigest(trusted.expected_manifest_sha256),
          "independent manifest pin required");
  const auto bytes = ReadBoundedFile(trusted.manifest_path, 4096);
  Require(Sha256(bytes) == trusted.expected_manifest_sha256, "manifest identity mismatch");
  std::string_view manifest = bytes;
  Require(TakeLine(manifest) == "ziliu-onnx-score-manifest-v1", "unsupported scorer manifest");
  constexpr std::array<std::string_view, 7> keys{
      "graph", "graph_sha256", "vocabulary", "vocabulary_sha256", "runtime", "runtime_sha256",
      "contract"};
  std::array<std::string, keys.size()> values;
  for (std::size_t i = 0; i < keys.size(); ++i) {
    const auto line = TakeLine(manifest);
    const std::string prefix = std::string(keys[i]) + '=';
    Require(line.starts_with(prefix), "unexpected manifest field or order");
    values[i] = line.substr(prefix.size());
  }
  Require(manifest.empty(), "unexpected manifest content");
  Require(SafeFilename(values[0]) && SafeFilename(values[2]) && SafeFilename(values[4]),
          "unsafe asset filename");
  Require(values[0] != values[2] && values[0] != values[4] && values[2] != values[4],
          "aliased manifest assets");
  Require(IsDigest(values[1]) && IsDigest(values[3]) && IsDigest(values[5]), "invalid asset digest");
  Require(values[6] == "causal-selected-mean-fp32-v1-ort1.20.1-opset20", "unsupported model contract");
  const auto root = trusted.manifest_path.parent_path();
  VerifiedScorerAssets result;
  result.manifest_sha256 = trusted.expected_manifest_sha256;
  result.graph = ReadBoundedFile(root / values[0], 32 * 1024 * 1024);
  Require(Sha256(result.graph) == values[1], "graph identity mismatch");
  const auto vocabulary = ReadBoundedFile(root / values[2], 1024 * 1024);
  Require(Sha256(vocabulary) == values[3], "vocabulary identity mismatch");
  result.vocabulary = ParseVocabulary(vocabulary);
  ValidateOnnxGraph(result.graph);
  result.runtime_path = root / values[4];
  result.runtime_sha256 = values[5];
  // RuntimeLibrary independently pins the open runtime file before code execution.
  Require(Sha256(ReadBoundedFile(result.runtime_path, 64 * 1024 * 1024)) == result.runtime_sha256,
          "runtime identity mismatch");
  return result;
}

}  // namespace ziliu::broker::detail
