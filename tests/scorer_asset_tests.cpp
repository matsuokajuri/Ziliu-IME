#include "ziliu/broker/onnx_scorer.h"
#include "scorer_assets.h"
#include "scorer_platform.h"

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace d = ziliu::broker::detail;
namespace s = ziliu::core::scoring;
namespace {
int checks = 0;
void Expect(bool condition, const char* message) {
  ++checks;
  if (!condition) throw std::runtime_error(message);
}
template <typename Function> void RejectReason(Function function, std::string_view expected) {
  bool rejected = false;
  try { function(); } catch (const std::exception& error) {
    rejected = true;
    Expect(error.what() == expected, "rejection came from the wrong validation stage");
  }
  Expect(rejected, "invalid asset accepted");
}
template <typename Function> void Rejects(Function function) {
  bool rejected = false;
  try { function(); } catch (const std::exception&) { rejected = true; }
  Expect(rejected, "invalid asset accepted");
}
struct TemporaryDirectory {
  std::filesystem::path path;
  TemporaryDirectory() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    path = std::filesystem::temp_directory_path() / ("ziliu-scorer-tests-" + std::to_string(stamp));
    Expect(std::filesystem::create_directory(path), "test directory creation");
    path = std::filesystem::canonical(path);
  }
  ~TemporaryDirectory() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
};
void Write(const std::filesystem::path& path, std::string_view value) {
  std::ofstream file(path, std::ios::binary);
  file.write(value.data(), static_cast<std::streamsize>(value.size()));
  Expect(static_cast<bool>(file), "test file write");
}
std::string Manifest(const std::string& graph = "model.onnx") {
  const std::string zero(64, '0');
  return "ziliu-onnx-score-manifest-v1\ngraph=" + graph + "\ngraph_sha256=" + zero +
         "\nvocabulary=vocabulary.tsv\nvocabulary_sha256=" + zero +
         "\nruntime=runtime.bin\nruntime_sha256=" + zero +
         "\ncontract=causal-selected-mean-fp32-v1-ort1.20.1-opset20\n";
}
// Minimal synthetic tensor graph for asset-format tests, never loaded as code.
std::string Varint(std::uint64_t value) {
  std::string result;
  while (value >= 128) {
    result += static_cast<char>((value & 127) | 128);
    value >>= 7;
  }
  result += static_cast<char>(value);
  return result;
}
std::string Integer(std::uint32_t field, std::uint64_t value) {
  return Varint(static_cast<std::uint64_t>(field) << 3) + Varint(value);
}
std::string Bytes(std::uint32_t field, std::string_view value) {
  return Varint((static_cast<std::uint64_t>(field) << 3) | 2) + Varint(value.size()) + std::string(value);
}
std::string SyntheticGraph() {
  const auto shape = Bytes(1, Integer(1, 1));
  const auto type = Bytes(2, Bytes(1, Integer(1, 1) + Bytes(2, shape)));
  const auto node = Bytes(1, "x") + Bytes(2, "y") + Bytes(4, "Identity");
  const auto graph = Bytes(1, node) + Bytes(2, "synthetic") +
                     Bytes(11, Bytes(1, "x") + type) + Bytes(12, Bytes(1, "y") + type);
  return Integer(1, 10) + Bytes(7, graph) + Bytes(8, Integer(2, 20));
}
std::string SyntheticVocabulary(bool duplicate_token = false, bool bad_order = false) {
  std::string result = "ziliu-scalar-vocabulary-v1\n";
  for (unsigned i = 0; i < 8189; ++i) {
    result += std::to_string(4096 + (bad_order && i == 1 ? 0 : i)) + '\t' +
              std::to_string(duplicate_token && i == 1 ? 3 : i + 3) + '\n';
  }
  return result;
}
std::string BoundManifest(std::string_view graph, std::string_view vocabulary,
                          std::string_view runtime) {
  return "ziliu-onnx-score-manifest-v1\ngraph=model.onnx\ngraph_sha256=" + d::Sha256(graph) +
         "\nvocabulary=vocabulary.tsv\nvocabulary_sha256=" + d::Sha256(vocabulary) +
         "\nruntime=runtime.bin\nruntime_sha256=" + d::Sha256(runtime) +
         "\ncontract=causal-selected-mean-fp32-v1-ort1.20.1-opset20\n";
}
void TestValidationStages(const std::filesystem::path& directory) {
  const auto graph = SyntheticGraph();
  const auto vocabulary = SyntheticVocabulary();
  const std::string runtime = "synthetic file; no executable code";
  const auto path = directory / "model.manifest";
  const auto manifest = BoundManifest(graph, vocabulary, runtime);
  Write(directory / "model.onnx", graph);
  Write(directory / "vocabulary.tsv", vocabulary);
  Write(directory / "runtime.bin", runtime);
  Write(path, manifest);
  Expect(d::LoadScorerAssets({path, d::Sha256(manifest)}).vocabulary.size() == 8189,
         "complete synthetic asset format");
  const auto reject = [&](const std::string& content, std::string_view reason) {
    Write(path, content);
    RejectReason([&] { (void)d::LoadScorerAssets({path, d::Sha256(content)}); }, reason);
  };
  reject(BoundManifest(graph + "changed", vocabulary, runtime), "graph identity mismatch");
  reject(BoundManifest(graph, vocabulary + "changed", runtime), "vocabulary identity mismatch");
  reject(BoundManifest(graph, vocabulary, runtime + "changed"), "runtime identity mismatch");
  for (const bool duplicate : {false, true}) {
    const auto invalid = SyntheticVocabulary(duplicate, !duplicate);
    Write(directory / "vocabulary.tsv", invalid);
    reject(BoundManifest(graph, invalid, runtime),
           duplicate ? "invalid vocabulary token" : "vocabulary order or duplicate");
  }
  Write(directory / "vocabulary.tsv", vocabulary);
  auto invalid = manifest;
  invalid.replace(invalid.find("causal-selected-mean-fp32-v1-ort1.20.1-opset20"),
                  std::string("causal-selected-mean-fp32-v1-ort1.20.1-opset20").size(), "other");
  reject(invalid, "unsupported model contract");
  invalid = manifest;
  invalid.replace(invalid.find("graph=model.onnx"), std::string("graph=model.onnx").size(),
                  "vocabulary=model.onnx");
  reject(invalid, "unexpected manifest field or order");
  reject(manifest + "extra=value\n", "unexpected manifest content");
  for (const auto* name : {"../model.onnx", "model..onnx", "/model.onnx"}) {
    invalid = manifest;
    invalid.replace(invalid.find("graph=model.onnx"), std::string("graph=model.onnx").size(),
                    std::string("graph=") + name);
    reject(invalid, "unsafe asset filename");
  }
  Write(path, manifest);
}
std::string Encode(std::uint32_t scalar) {
  std::string text;
  if (scalar < 128) text += static_cast<char>(scalar);
  else if (scalar < 2048) {
    text += static_cast<char>(0xc0 | (scalar >> 6));
    text += static_cast<char>(0x80 | (scalar & 63));
  } else if (scalar < 65536) {
    text += static_cast<char>(0xe0 | (scalar >> 12));
    text += static_cast<char>(0x80 | ((scalar >> 6) & 63));
    text += static_cast<char>(0x80 | (scalar & 63));
  } else {
    text += static_cast<char>(0xf0 | (scalar >> 18));
    text += static_cast<char>(0x80 | ((scalar >> 12) & 63));
    text += static_cast<char>(0x80 | ((scalar >> 6) & 63));
    text += static_cast<char>(0x80 | (scalar & 63));
  }
  return text;
}
}

int main(int argc, char** argv) {
  try {
    Expect(d::Sha256("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
           "empty SHA-256 known-answer");
    Expect(d::Sha256("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
           "SHA-256 known-answer");
    TemporaryDirectory temporary;
    const auto manifest_path = temporary.path / "model.manifest";
    Write(manifest_path, Manifest());
    Rejects([&] { (void)d::LoadScorerAssets({manifest_path, ""}); });
    Rejects([&] { (void)d::LoadScorerAssets({manifest_path, std::string(64, '0')}); });
    Rejects([&] { (void)d::LoadScorerAssets({"model.manifest", d::Sha256(Manifest())}); });
    for (const auto& bad : {Manifest("../model.onnx"), Manifest("model..onnx"),
                            Manifest("/model.onnx"), Manifest("model.onnx") + "extra=value\n",
                            std::string("ziliu-onnx-score-manifest-v2\n"),
                            std::string("ziliu-onnx-score-manifest-v1\r\n")}) {
      Write(manifest_path, bad);
      Rejects([&] { (void)d::LoadScorerAssets({manifest_path, d::Sha256(bad)}); });
    }
    TestValidationStages(temporary.path);
    const auto file = temporary.path / "bytes.bin";
    Write(file, "abc");
    Expect(d::ReadBoundedFile(file, 3) == "abc", "exact bounded read");
    Rejects([&] { (void)d::ReadBoundedFile(file, 2); });
    Rejects([&] { (void)d::ReadBoundedFile(temporary.path, 100); });
    Rejects([&] { d::RuntimeLibrary runtime(file, std::string(64, '0')); });
    std::error_code symlink_error;
    std::filesystem::create_symlink(file, temporary.path / "alias.bin", symlink_error);
    if (!symlink_error) Rejects([&] { (void)d::ReadBoundedFile(temporary.path / "alias.bin", 3); });

    // Opt-in external fixture only. Never run from normal CTest and never accept
    // a manifest's own digest as its approval. Caller supplies both arguments.
    if (argc == 3) {
      const ziliu::broker::TrustedScorerAssets trusted{std::filesystem::path(argv[1]), argv[2]};
      const auto assets = d::LoadScorerAssets(trusted);
      const auto first = assets.vocabulary.begin()->first;
      auto second_iterator = assets.vocabulary.begin();
      ++second_iterator;
      const auto second = second_iterator->first;
      auto scorer = ziliu::broker::CreateOnnxScorer(trusted, {7, 1}, 2);
      s::Request request{{7, 1}, 1, s::Privacy::kOrdinary, "",
                          {{0, Encode(first)}, {3, Encode(second)}, {8, Encode(first)}}};
      const auto result = scorer->Score(request);
      Expect(result.status == s::Status::kScored && result.scores.size() == 3,
             "external ORT synthetic request");
      Expect(result.scores[0] == result.scores[2] && result.source_indices ==
                 std::vector<std::int64_t>{0, 3, 8}, "external ORT duplicate and source identity");
      scorer->Close();
      std::cout << "external ORT synthetic check passed\n";
    } else {
      Expect(argc == 1, "usage: scorer_asset_tests [absolute-manifest independent-sha256]");
    }
    std::cout << "scorer asset checks passed: " << checks << '\n';
    return EXIT_SUCCESS;
  } catch (const std::exception& error) {
    std::cerr << "scorer asset test failed: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
