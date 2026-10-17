#include "../src/broker/src/onnx_graph_gate.h"

#include <bit>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

std::size_t checks = 0;

std::string Varint(std::uint64_t value) {
  std::string result;
  while (value >= 128) {
    result.push_back(static_cast<char>((value & 127U) | 128U));
    value >>= 7U;
  }
  result.push_back(static_cast<char>(value));
  return result;
}

std::string Integer(std::uint32_t field, std::uint64_t value) {
  return Varint(static_cast<std::uint64_t>(field) << 3U) + Varint(value);
}

std::string Bytes(std::uint32_t field, std::string_view value) {
  return Varint((static_cast<std::uint64_t>(field) << 3U) | 2U) + Varint(value.size()) +
         std::string(value);
}

std::string Float(std::uint32_t field, float value) {
  auto result = Varint((static_cast<std::uint64_t>(field) << 3U) | 5U);
  auto bits = std::bit_cast<std::uint32_t>(value);
  for (unsigned index = 0; index != 4; ++index) {
    result.push_back(static_cast<char>(bits & 255U));
    bits >>= 8U;
  }
  return result;
}

std::string Metadata() {
  return Bytes(1, "key") + Bytes(2, "value");
}

std::string Value(std::string_view name, std::uint64_t type = 1) {
  const auto shape = Bytes(1, Integer(1, 1));
  const auto tensor_type = Integer(1, type) + Bytes(2, shape);
  return Bytes(1, name) + Bytes(2, Bytes(1, tensor_type));
}

std::string Tensor(std::uint64_t type = 1) {
  const std::size_t width = type == 7 ? 8 : type == 9 ? 1 : 4;
  return Integer(2, type) + Bytes(9, std::string(width, '\0'));
}

std::string Attribute(std::string_view name, std::uint64_t type, std::string_view payload) {
  return Bytes(1, name) + Integer(20, type) + std::string(payload);
}

std::string Node(std::string_view op = "Identity", std::string_view extra = {}) {
  return Bytes(1, "x") + Bytes(2, "y") + Bytes(4, op) + std::string(extra);
}

std::string Graph(std::string_view node, std::string_view extra = {}) {
  return Bytes(1, node) + Bytes(2, "synthetic") + Bytes(11, Value("x")) +
         Bytes(12, Value("y")) + std::string(extra);
}

std::string Model(std::string_view graph) {
  return Integer(1, 10) + Bytes(7, graph) + Bytes(8, Integer(2, 20));
}

std::string Minimal() {
  return Model(Graph(Node()));
}

std::string Constant(std::string_view tensor) {
  return Model(Graph(Node("Constant", Bytes(5, Attribute("value", 4, Bytes(5, tensor))))));
}

std::string WithAttribute(std::string_view op, std::string_view name, std::uint64_t type,
                          std::string_view payload) {
  return Model(Graph(Node(op, Bytes(5, Attribute(name, type, payload)))));
}

[[noreturn]] void Fail(std::string_view description) {
  std::cerr << "FAILED: " << description << '\n';
  std::exit(EXIT_FAILURE);
}

void Accept(std::string_view bytes, std::string_view description) {
  ++checks;
  try {
    ziliu::broker::detail::ValidateOnnxGraph(bytes);
  } catch (const std::runtime_error&) {
    Fail(description);
  }
}

void Reject(std::string_view bytes, std::string_view description) {
  ++checks;
  try {
    ziliu::broker::detail::ValidateOnnxGraph(bytes);
  } catch (const std::runtime_error& error) {
    if (std::string_view(error.what()) !=
        "ONNX graph rejected: unsupported or malformed self-contained format") {
      Fail("rejections must not include model contents or sensitive paths");
    }
    return;
  }
  Fail(description);
}

void TestAcceptedSubset() {
  Accept(Minimal(), "minimal standard tensor graph");
  Accept(Model(Graph(Node("Identity", Bytes(7, "")))), "explicit empty standard domain");
  Accept(Minimal() + Bytes(2, "synthetic") + Bytes(3, "1") + Bytes(4, "") + Integer(5, 1) +
             Bytes(6, "documentation") + Bytes(14, Metadata()),
         "known non-executable model metadata");
  Accept(Model(Graph(Node("Identity", Bytes(9, Metadata())), Bytes(16, Metadata()))),
         "graph and node metadata");
  for (const auto type : {1U, 6U, 7U, 9U}) {
    Accept(Constant(Tensor(type)), "embedded supported raw tensor type");
  }
  Accept(Constant(Integer(1, 1) + Integer(1, 1) + Tensor()), "unpacked tensor dimensions");
  Accept(Constant(Bytes(1, Varint(1) + Varint(1)) + Tensor()), "packed tensor dimensions");
  Accept(Constant(Integer(1, 0) + Integer(2, 1) + Bytes(9, "")), "empty raw zero-size tensor");
  Accept(Model(Graph(Node(), Bytes(5, Tensor() + Bytes(8, "weight")))),
         "embedded graph initializer");
  Accept(WithAttribute("Cast", "to", 2, Integer(3, 7)),
         "cast to INT64 index type");
  Accept(WithAttribute("Gelu", "approximate", 3, Bytes(4, "none")),
         "required non-tensor Gelu string attribute");
  Accept(Model(Graph(Node("LayerNormalization",
                          Bytes(5, Attribute("epsilon", 1, Float(2, 0.00001F))) +
                              Bytes(5, Attribute("stash_type", 2, Integer(3, 1)))))),
         "FP32 layer normalization attributes");
  const auto minus_one = std::numeric_limits<std::uint64_t>::max();
  Accept(WithAttribute("Constant", "value_ints", 7, Bytes(8, Varint(minus_one) + Varint(1))),
         "packed int64 constants including negative signed values");
  Accept(WithAttribute("Constant", "value_ints", 7, Integer(8, minus_one) + Integer(8, 1)),
         "unpacked int64 constants");
  const auto dynamic_shape = Bytes(1, Bytes(2, "batch")) + Bytes(1, Integer(1, 4));
  const auto dynamic_value = Bytes(1, "dynamic") +
                             Bytes(2, Bytes(1, Integer(1, 1) + Bytes(2, dynamic_shape)));
  Accept(Model(Graph(Node(), Bytes(13, dynamic_value))), "symbolic tensor dimensions");
  const auto unknown_rank = Bytes(1, "shape_calculation") + Bytes(2, Bytes(1, Integer(1, 7)));
  Accept(Model(Graph(Node(), Bytes(13, unknown_rank))), "unknown-rank intermediate tensor");
  Reject(Model(Graph(Node(), Bytes(11, unknown_rank))), "input tensor shape must be present");
  Reject(Model(Graph(Node(), Bytes(12, unknown_rank))), "output tensor shape must be present");
}

void TestDomainsAndOperators() {
  Reject(Model(Graph(Node("Identity", Bytes(7, "private.custom")))), "custom operator domain");
  Reject(Model(Graph(Node("Identity", Bytes(7, "ai.onnx.ml")))), "unsupported standard domain");
  Reject(Model(Graph(Node("PrivateFileLoader"))), "custom operator");
  Reject(Model(Graph(Node("If"))), "control-flow operator outside allowlist");
  Reject(Model(Graph(Node("Identity", Bytes(8, "overload")))), "operator overload");
  Reject(Model(Graph(Node("Identity", Bytes(10, "")))), "node device configuration");
  Reject(Integer(1, 10) + Bytes(7, Graph(Node())) +
             Bytes(8, Bytes(1, "private.custom") + Integer(2, 20)),
         "custom opset import");
  Reject(Integer(1, 10) + Bytes(7, Graph(Node())) + Bytes(8, Integer(2, 19)),
         "unreviewed opset version");
  Reject(Integer(1, 11) + Bytes(7, Graph(Node())) + Bytes(8, Integer(2, 20)),
         "unreviewed IR version");
  Reject(Minimal() + Bytes(8, Integer(2, 20)), "multiple opset imports");
  Reject(Minimal() + Bytes(4, "private.custom"), "nonempty model domain");
  Reject(Model(Graph(Node("Identity", Bytes(4, "Identity")))), "duplicate operator field");
  Reject(Model(Graph(Node("Identity", Bytes(7, "") + Bytes(7, "")))),
         "duplicate domain field cannot merge");
}

void TestExternalAndUnsupportedTensors() {
  const auto external = Bytes(13, Bytes(1, "location") + Bytes(2, "sensitive-path-marker"));
  Reject(Model(Graph(Node(), Bytes(5, Tensor() + external))), "external initializer data");
  Reject(Constant(Tensor() + external), "external tensor attribute data");
  for (const auto location : {0U, 1U, 2U}) {
    Reject(Model(Graph(Node(), Bytes(5, Tensor() + Integer(14, location)))),
           "initializer data_location is not permitted");
    Reject(Constant(Tensor() + Integer(14, location)),
           "attribute tensor data_location is not permitted");
  }
  Reject(Constant(Integer(2, 8) + Bytes(6, "private string")), "string tensor data");
  for (const auto type : {0U, 2U, 3U, 4U, 5U, 8U, 10U, 11U, 12U, 13U, 14U, 16U, 21U}) {
    Reject(Constant(Tensor(type)), "unsupported tensor data type");
  }
  Reject(Constant(Tensor() + Bytes(3, "")), "tensor segment");
  Reject(Constant(Integer(2, 1) + Float(4, 1)), "typed repeated tensor data is unsupported");
  Reject(Constant(Tensor() + Integer(2, 1)), "duplicate tensor data type");
  Reject(Constant(Tensor() + Bytes(9, std::string(4, '\0'))), "duplicate tensor raw payload");
  Reject(Constant(Integer(2, 1) + Bytes(9, "")), "raw scalar tensor length mismatch");
  Reject(Constant(Bytes(9, std::string(4, '\0'))), "tensor type is required");
  Reject(Constant(Integer(2, 1)), "tensor raw payload is required");
  Reject(Model(Graph(Node(), Bytes(15, ""))), "sparse graph initializer");
  Reject(Model(Graph(Node(), Bytes(14, ""))), "graph quantization annotation");
  Reject(WithAttribute("Constant", "value", 9, Bytes(10, Tensor() + external)),
         "tensor-list attributes are not supported, including hidden external tensors");
  Reject(WithAttribute("Constant", "value", 2, Bytes(5, Tensor() + external)),
         "external tensor cannot hide behind mismatched attribute type");
}

void TestAttributesAndTypes() {
  Reject(WithAttribute("Identity", "body", 5, Bytes(6, Graph(Node()))),
         "nested graph attribute");
  Reject(WithAttribute("Identity", "branches", 10, Bytes(11, "")),
         "nested graph-list attribute");
  Reject(WithAttribute("Constant", "value", 11, Bytes(22, "")),
         "sparse tensor attribute");
  Reject(WithAttribute("Identity", "type", 13, Bytes(14, "")),
         "type-proto attribute");
  Reject(Model(Graph(Node("Cast", Bytes(5, Attribute("to", 2, Integer(3, 1)) + Bytes(21, "ref"))))),
         "attribute reference");
  Reject(WithAttribute("Cast", "to", 2, Integer(3, 8)),
         "cast to string tensor");
  Reject(WithAttribute("Cast", "to", 2, Integer(3, 10)),
         "cast to reduced precision tensor");
  Reject(Model(Graph(Node("Cast"))), "required cast destination");
  Reject(Model(Graph(Node("Constant"))), "required constant payload");
  Reject(Model(Graph(Node("Concat"))), "required concatenation axis");
  Reject(WithAttribute("Gelu", "approximate", 3, Bytes(4, "tanh")),
         "unreviewed Gelu approximation");
  Reject(WithAttribute("LayerNormalization", "stash_type", 2, Integer(3, 10)),
         "reduced-precision stash type");
  Reject(WithAttribute("LayerNormalization", "epsilon", 1,
                       Float(2, std::numeric_limits<float>::infinity())),
         "non-finite floating attribute");
  Reject(WithAttribute("Reshape", "allowzero", 2, Integer(3, 2)),
         "non-boolean reshape setting");
  const auto to = Bytes(5, Attribute("to", 2, Integer(3, 1)));
  Reject(Model(Graph(Node("Cast", to + to))), "duplicate attribute names");
  Reject(WithAttribute("Cast", "to", 2, Integer(3, 1) + Integer(3, 1)),
         "duplicate attribute payload");
  Reject(Model(Graph(Node("Cast", Bytes(5, Attribute("to", 2, Integer(3, 1)) + Integer(20, 2))))),
         "duplicate attribute type");
  Reject(WithAttribute("Identity", "unrecognized", 2, Integer(3, 1)),
         "unknown attribute on permitted operator");
  Reject(Model(Graph(Node(), Bytes(13, Value("strings", 8)))), "string value declaration");
  for (const auto alternative : {4U, 5U, 7U, 8U, 9U}) {
    const auto value = Bytes(1, "unsupported") + Bytes(2, Bytes(alternative, ""));
    Reject(Model(Graph(Node(), Bytes(13, value))), "non-tensor value type");
  }
  const auto ambiguous_dimension = Integer(1, 1) + Bytes(2, "dynamic");
  const auto ambiguous_type = Integer(1, 1) + Bytes(2, Bytes(1, ambiguous_dimension));
  Reject(Model(Graph(Node(), Bytes(13, Bytes(1, "ambiguous") + Bytes(2, Bytes(1, ambiguous_type))))),
         "duplicate dimension oneof alternatives");
}

void TestSchemaAndWireFormat() {
  Reject("", "empty model");
  Reject(Integer(1, 10), "missing graph and opset");
  Reject(Model(""), "empty graph");
  Reject(Minimal() + Integer(1, 10), "duplicate IR version");
  Reject(Minimal() + Bytes(7, Graph(Node())), "duplicate graph");
  for (const auto field : {20U, 25U, 26U, 127U}) {
    Reject(Minimal() + Bytes(field, ""), "training, functions, devices and unknown model fields");
  }
  Reject(Model(Graph(Node(), Bytes(127, ""))), "unknown graph field");
  Reject(Model(Graph(Node("Identity", Bytes(127, "")))), "unknown node field");
  Reject(Minimal() + Bytes(14, Metadata() + Bytes(127, "")), "unknown metadata field");
  Reject(Minimal() + Bytes(2, std::string("secret\0path", 11)), "embedded NUL in text");
  Reject(Integer(1, 10) + Integer(7, 0) + Bytes(8, Integer(2, 20)), "incorrect graph wire type");
  Reject(Bytes(1, "10") + Bytes(7, Graph(Node())) + Bytes(8, Integer(2, 20)),
         "incorrect IR wire type");
  Reject(std::string(1, '\0'), "field zero");
  Reject(std::string(1, static_cast<char>(0x80)), "truncated varint");
  Reject(std::string(10, static_cast<char>(0x80)), "varint exceeding 64 bits");
  Reject(std::string(9, static_cast<char>(0xFF)) + static_cast<char>(2), "overflowing tenth byte");
  Reject(std::string({static_cast<char>(0x88), '\0', '\x0A'}), "noncanonical field key");
  Reject(Integer(1, 10) + std::string({static_cast<char>(0x3A), static_cast<char>(0x80)}),
         "truncated length varint");
  Reject(Integer(1, 10) + Varint(58) + Varint(std::numeric_limits<std::uint64_t>::max()),
         "length cannot exceed remaining bytes or overflow size conversion");
  Reject(Integer(1, 10) + Varint(58) + Varint(5) + "ab", "truncated length-delimited message");
  Reject(Varint((static_cast<std::uint64_t>(0x20000000U) << 3U) | 2U) + Varint(0),
         "field number exceeds protobuf maximum");
  for (const auto wire : {3U, 4U, 6U, 7U}) {
    Reject(Varint(8U | wire), "groups and invalid wire types");
  }
  Reject(Varint(9) + "abc", "truncated fixed64 field");
  Reject(Varint(13) + "abc", "truncated fixed32 field");
  auto truncated = Minimal();
  truncated.pop_back();
  Reject(truncated, "truncated nested message");
  Reject(Minimal() + std::string(1, static_cast<char>(0x80)), "malformed trailing field");
}

void TestBounds() {
  Reject(std::string(64U * 1024U * 1024U + 1U, '\0'), "model byte limit");
  Reject(Minimal() + Bytes(2, std::string(65537, 'a')), "text byte limit");
  Reject(Constant(Integer(1, 1048577) + Tensor()), "tensor dimension limit");
  Reject(Constant(Integer(1, 1048576) + Integer(1, 1048576) + Tensor()),
         "tensor element product limit");
  std::string dimensions;
  for (unsigned index = 0; index != 9; ++index) {
    dimensions += Integer(1, 1);
  }
  Reject(Constant(dimensions + Tensor()), "tensor rank limit");
  std::string inputs;
  for (unsigned index = 0; index != 16; ++index) {
    inputs += Bytes(11, Value("extra"));
  }
  Reject(Model(Graph(Node(), inputs)), "graph input count limit");
  std::string nodes;
  for (unsigned index = 0; index != 4096; ++index) {
    nodes += Bytes(1, Node());
  }
  Reject(Model(Graph(Node(), nodes)), "graph node count limit");
  std::string integer_values;
  for (unsigned index = 0; index != 257; ++index) {
    integer_values += Varint(0);
  }
  Reject(WithAttribute("Constant", "value_ints", 7, Bytes(8, integer_values)),
         "packed integer count limit");
  auto many_fields = Minimal();
  for (unsigned index = 0; index != 20000; ++index) {
    many_fields += Bytes(14, Metadata());
  }
  Reject(many_fields, "global wire parsing work budget");
}

}  // namespace

int main(int argc, char** argv) {
  if (argc > 2) {
    Fail("expected at most one optional graph file");
  }
  TestAcceptedSubset();
  TestDomainsAndOperators();
  TestExternalAndUnsupportedTensors();
  TestAttributesAndTypes();
  TestSchemaAndWireFormat();
  TestBounds();
  if (argc == 2) {
    // Optional local integration check. Never record paths, weights or hashes
    // in the test output or source; no inference runtime is initialized.
    std::ifstream stream(argv[1], std::ios::binary | std::ios::ate);
    const auto length = stream.tellg();
    if (!stream || length <= 0 || length > 64 * 1024 * 1024) {
      Fail("optional graph must be readable and bounded");
    }
    std::string bytes(static_cast<std::size_t>(length), '\0');
    stream.seekg(0);
    stream.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!stream || stream.peek() != std::char_traits<char>::eof()) {
      Fail("optional graph read must be complete");
    }
    Accept(bytes, "optional self-contained graph format");
  }
  std::cout << "ONNX graph gate: " << checks << " checks passed\n";
  return EXIT_SUCCESS;
}
