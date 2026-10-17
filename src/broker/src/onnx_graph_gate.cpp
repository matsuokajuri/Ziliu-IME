#include "onnx_graph_gate.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace ziliu::broker::detail {
namespace {

constexpr std::size_t kMaxGraphBytes = 64U * 1024U * 1024U;
constexpr std::size_t kMaxFields = 100000;
constexpr std::size_t kMaxTextBytes = 65536;
constexpr std::size_t kMaxRank = 8;
constexpr std::uint64_t kMaxDimension = 1048576;
constexpr std::uint64_t kMaxTensorElements = 16U * 1024U * 1024U;

[[noreturn]] void Reject() {
  throw std::runtime_error("ONNX graph rejected: unsupported or malformed self-contained format");
}

void Require(bool condition) {
  if (!condition) {
    Reject();
  }
}

void Once(bool& seen) {
  Require(!seen);
  seen = true;
}

struct Budget {
  std::size_t fields = kMaxFields;

  void Consume() {
    Require(fields != 0);
    --fields;
  }
};

struct Field {
  std::uint32_t number = 0;
  std::uint32_t wire = 0;
  std::uint64_t integer = 0;
  std::string_view bytes;
};

// Views never own or copy tensor payloads. Nested message calls below have a
// fixed maximum depth: subgraphs, functions and recursive type features fail.
class Reader {
 public:
  Reader(std::string_view bytes, Budget& budget) : bytes_(bytes), budget_(budget) {}

  bool Empty() const {
    return bytes_.empty();
  }

  std::uint64_t Varint() {
    budget_.Consume();
    std::uint64_t value = 0;
    for (unsigned index = 0; index != 10; ++index) {
      Require(!bytes_.empty());
      const auto byte = static_cast<unsigned char>(bytes_.front());
      bytes_.remove_prefix(1);
      Require(index != 9 || byte <= 1);
      value |= static_cast<std::uint64_t>(byte & 0x7FU) << (index * 7U);
      if ((byte & 0x80U) == 0) {
        Require(index == 0 || byte != 0);  // Reject non-canonical varints.
        return value;
      }
    }
    Reject();
  }

  Field Next() {
    const auto key = Varint();
    Require((key >> 3U) != 0 && (key >> 3U) <= 0x1FFFFFFFU);
    Field field;
    field.number = static_cast<std::uint32_t>(key >> 3U);
    field.wire = static_cast<std::uint32_t>(key & 7U);
    switch (field.wire) {
      case 0:
        field.integer = Varint();
        break;
      case 1:
        field.bytes = Take(8);
        break;
      case 2: {
        const auto length = Varint();
        Require(length <= bytes_.size());
        field.bytes = Take(static_cast<std::size_t>(length));
        break;
      }
      case 5:
        field.bytes = Take(4);
        break;
      default:
        Reject();  // Groups and invalid wire types have no accepted use.
    }
    return field;
  }

 private:
  std::string_view Take(std::size_t count) {
    Require(count <= bytes_.size());
    const auto result = bytes_.substr(0, count);
    bytes_.remove_prefix(count);
    return result;
  }

  std::string_view bytes_;
  Budget& budget_;
};

std::uint64_t Integer(const Field& field) {
  Require(field.wire == 0);
  return field.integer;
}

std::string_view Message(const Field& field) {
  Require(field.wire == 2);
  return field.bytes;
}

std::string_view Text(const Field& field) {
  const auto text = Message(field);
  Require(text.size() <= kMaxTextBytes && text.find('\0') == std::string_view::npos);
  return text;
}

bool IsTensorType(std::uint64_t type) {
  // FP32 arithmetic, with INT32/INT64 indices and BOOL masks. No reduced
  // precision, strings, complex, opaque, quantized or unsigned tensor types.
  return type == 1 || type == 6 || type == 7 || type == 9;
}

template <typename Function>
void Integers(const Field& field, Budget& budget, Function consume) {
  if (field.wire == 0) {
    consume(field.integer);
    return;
  }
  Reader packed(Message(field), budget);
  while (!packed.Empty()) {
    consume(packed.Varint());
  }
}

void ValidateMetadata(std::string_view bytes, Budget& budget) {
  Reader reader(bytes, budget);
  bool key = false;
  bool value = false;
  while (!reader.Empty()) {
    const auto field = reader.Next();
    if (field.number == 1) {
      Once(key);
      Require(!Text(field).empty());
    } else if (field.number == 2) {
      Once(value);
      static_cast<void>(Text(field));
    } else {
      Reject();
    }
  }
  Require(key && value);
}

void ValidateTensor(std::string_view bytes, Budget& budget) {
  Reader reader(bytes, budget);
  std::size_t rank = 0;
  std::uint64_t elements = 1;
  std::uint64_t type = 0;
  std::size_t payload_bytes = 0;
  bool type_seen = false;
  bool raw_seen = false;
  bool name_seen = false;
  bool doc_seen = false;
  while (!reader.Empty()) {
    const auto field = reader.Next();
    switch (field.number) {
      case 1:
        Integers(field, budget, [&](std::uint64_t dimension) {
          Require(++rank <= kMaxRank && dimension <= kMaxDimension);
          Require(dimension == 0 || elements <= kMaxTensorElements / dimension);
          elements *= dimension;
        });
        break;
      case 2:
        Once(type_seen);
        type = Integer(field);
        Require(IsTensorType(type));
        break;
      case 8:
        Once(name_seen);
        static_cast<void>(Text(field));
        break;
      case 9:
        Once(raw_seen);
        payload_bytes = Message(field).size();
        break;
      case 12:
        Once(doc_seen);
        static_cast<void>(Text(field));
        break;
      case 16:
        ValidateMetadata(Message(field), budget);
        break;
      default:
        // In particular reject segment, external_data and data_location,
        // including an explicitly encoded DEFAULT location. Only embedded raw
        // tensors are supported; typed repeated payloads are intentionally out.
        Reject();
    }
  }
  Require(type_seen && raw_seen);
  const std::uint64_t width = type == 7 ? 8 : type == 9 ? 1 : 4;
  Require(elements * width == payload_bytes);
}

void ValidateDimension(std::string_view bytes, Budget& budget) {
  Reader reader(bytes, budget);
  bool value_seen = false;
  while (!reader.Empty()) {
    const auto field = reader.Next();
    Once(value_seen);  // dim_value and dim_param are oneof alternatives.
    if (field.number == 1) {
      Require(Integer(field) <= kMaxDimension);
    } else if (field.number == 2) {
      const auto parameter = Text(field);
      Require(!parameter.empty() && parameter.size() <= 256);
    } else {
      Reject();
    }
  }
  Require(value_seen);
}

void ValidateShape(std::string_view bytes, Budget& budget) {
  Reader reader(bytes, budget);
  std::size_t rank = 0;
  while (!reader.Empty()) {
    const auto field = reader.Next();
    Require(field.number == 1 && ++rank <= kMaxRank);
    ValidateDimension(Message(field), budget);
  }
}

void ValidateTensorType(std::string_view bytes, Budget& budget, bool require_shape) {
  Reader reader(bytes, budget);
  bool type_seen = false;
  bool shape_seen = false;
  while (!reader.Empty()) {
    const auto field = reader.Next();
    if (field.number == 1) {
      Once(type_seen);
      Require(IsTensorType(Integer(field)));
    } else if (field.number == 2) {
      Once(shape_seen);
      ValidateShape(Message(field), budget);
    } else {
      Reject();
    }
  }
  // ONNX permits an unknown-rank intermediate tensor to omit its shape. The
  // supported exporter uses this for intermediate INT64 shape calculations.
  // Public graph input/output declarations must still specify their shapes.
  Require(type_seen && (shape_seen || !require_shape));
}

void ValidateType(std::string_view bytes, Budget& budget, bool require_shape) {
  Reader reader(bytes, budget);
  bool tensor_seen = false;
  bool denotation_seen = false;
  while (!reader.Empty()) {
    const auto field = reader.Next();
    if (field.number == 1) {
      Once(tensor_seen);
      ValidateTensorType(Message(field), budget, require_shape);
    } else if (field.number == 6) {
      Once(denotation_seen);
      static_cast<void>(Text(field));
    } else {
      Reject();
    }
  }
  Require(tensor_seen);
}

void ValidateValueInfo(std::string_view bytes, Budget& budget, bool require_shape) {
  Reader reader(bytes, budget);
  bool name_seen = false;
  bool type_seen = false;
  bool doc_seen = false;
  while (!reader.Empty()) {
    const auto field = reader.Next();
    switch (field.number) {
      case 1:
        Once(name_seen);
        Require(!Text(field).empty());
        break;
      case 2:
        Once(type_seen);
        ValidateType(Message(field), budget, require_shape);
        break;
      case 3:
        Once(doc_seen);
        static_cast<void>(Text(field));
        break;
      case 4:
        ValidateMetadata(Message(field), budget);
        break;
      default:
        Reject();
    }
  }
  Require(name_seen && type_seen);
}

struct Attribute {
  std::string_view name;
  std::uint64_t type = 0;
  std::uint64_t integer = 0;
  std::string_view text;
  float floating = 0;
};

Attribute ValidateAttribute(std::string_view bytes, Budget& budget) {
  Reader reader(bytes, budget);
  Attribute attribute;
  bool name_seen = false;
  bool type_seen = false;
  bool doc_seen = false;
  std::uint64_t payload_type = 0;
  std::size_t integer_count = 0;
  while (!reader.Empty()) {
    const auto field = reader.Next();
    switch (field.number) {
      case 1:
        Once(name_seen);
        attribute.name = Text(field);
        Require(!attribute.name.empty());
        break;
      case 2: {
        Require(payload_type == 0 && field.wire == 5);
        payload_type = 1;
        std::uint32_t bits = 0;
        for (unsigned index = 0; index != 4; ++index) {
          bits |= static_cast<std::uint32_t>(static_cast<unsigned char>(field.bytes[index]))
                  << (index * 8U);
        }
        attribute.floating = std::bit_cast<float>(bits);
        Require(std::isfinite(attribute.floating));
        break;
      }
      case 3:
        Require(payload_type == 0);
        payload_type = 2;
        attribute.integer = Integer(field);
        break;
      case 4:
        Require(payload_type == 0);
        payload_type = 3;
        attribute.text = Text(field);
        break;
      case 5:
        Require(payload_type == 0);
        payload_type = 4;
        ValidateTensor(Message(field), budget);
        break;
      case 8:
        Require(payload_type == 0 || payload_type == 7);
        payload_type = 7;
        Integers(field, budget, [&](std::uint64_t) { Require(++integer_count <= 256); });
        break;
      case 13:
        Once(doc_seen);
        static_cast<void>(Text(field));
        break;
      case 20:
        Once(type_seen);
        attribute.type = Integer(field);
        break;
      default:
        // Reject graph(s), sparse tensor(s), type proto(s), tensor lists,
        // references and all other unsupported payloads, regardless of type.
        Reject();
    }
  }
  Require(name_seen && type_seen && payload_type != 0 && attribute.type == payload_type);
  return attribute;
}

bool AllowedOperator(std::string_view name) {
  constexpr std::array names = {
      "Add", "Cast", "CastLike", "Concat", "Constant", "Div", "Gather", "GatherElements",
      "Gelu", "Identity", "LayerNormalization", "LessOrEqual", "LogSoftmax", "MatMul",
      "Not", "Range", "ReduceSum", "Reshape", "Shape", "Slice", "Softmax", "Squeeze",
      "Transpose", "Unsqueeze", "Where"};
  return std::find(names.begin(), names.end(), name) != names.end();
}

void ValidateOperatorAttributes(std::string_view op, const Attribute* attributes,
                                std::size_t count) {
  bool required_seen = false;
  for (std::size_t index = 0; index != count; ++index) {
    const auto& attribute = attributes[index];
    const auto name = attribute.name;
    for (std::size_t previous = 0; previous != index; ++previous) {
      Require(attributes[previous].name != name);
    }
    if (op == "Constant") {
      Require(count == 1 && ((name == "value" && attribute.type == 4) ||
                             (name == "value_ints" && attribute.type == 7)));
      required_seen = true;
    } else if (op == "Cast" && name == "to") {
      Require(attribute.type == 2 && IsTensorType(attribute.integer));
      required_seen = true;
    } else if (op == "Gelu" && name == "approximate") {
      Require(attribute.type == 3 && attribute.text == "none");
    } else if (op == "LayerNormalization" && name == "epsilon") {
      Require(attribute.type == 1 && attribute.floating > 0);
    } else if (op == "LayerNormalization" && name == "stash_type") {
      Require(attribute.type == 2 && attribute.integer == 1);
    } else if (op == "Transpose" && name == "perm") {
      Require(attribute.type == 7);
    } else if (op == "Shape" && (name == "start" || name == "end")) {
      Require(attribute.type == 2);
    } else if ((op == "Reshape" && name == "allowzero") ||
               (op == "ReduceSum" && (name == "keepdims" || name == "noop_with_empty_axes"))) {
      Require(attribute.type == 2 && attribute.integer <= 1);
    } else if (name == "axis" && (op == "Gather" || op == "GatherElements" || op == "Concat" ||
                                 op == "Softmax" || op == "LogSoftmax" ||
                                 op == "LayerNormalization")) {
      Require(attribute.type == 2);
      required_seen = true;
    } else {
      Reject();
    }
  }
  if (op == "Constant" || op == "Cast" || op == "Concat") {
    Require(required_seen);
  }
}

void ValidateNode(std::string_view bytes, Budget& budget) {
  Reader reader(bytes, budget);
  std::array<Attribute, 8> attributes;
  std::size_t attribute_count = 0;
  std::size_t inputs = 0;
  std::size_t outputs = 0;
  std::string_view op;
  bool op_seen = false;
  bool name_seen = false;
  bool domain_seen = false;
  bool doc_seen = false;
  while (!reader.Empty()) {
    const auto field = reader.Next();
    switch (field.number) {
      case 1:
        Require(++inputs <= 16);
        static_cast<void>(Text(field));
        break;
      case 2:
        Require(++outputs <= 16);
        Require(!Text(field).empty());
        break;
      case 3:
        Once(name_seen);
        static_cast<void>(Text(field));
        break;
      case 4:
        Once(op_seen);
        op = Text(field);
        Require(AllowedOperator(op));
        break;
      case 5:
        Require(attribute_count < attributes.size());
        attributes[attribute_count++] = ValidateAttribute(Message(field), budget);
        break;
      case 6:
        Once(doc_seen);
        static_cast<void>(Text(field));
        break;
      case 7:
        Once(domain_seen);
        Require(Text(field).empty());
        break;
      case 9:
        ValidateMetadata(Message(field), budget);
        break;
      default:
        Reject();
    }
  }
  Require(op_seen && outputs != 0);
  ValidateOperatorAttributes(op, attributes.data(), attribute_count);
}

void ValidateGraph(std::string_view bytes, Budget& budget) {
  Reader reader(bytes, budget);
  std::size_t nodes = 0;
  std::size_t tensors = 0;
  std::size_t inputs = 0;
  std::size_t outputs = 0;
  std::size_t values = 0;
  bool name_seen = false;
  bool doc_seen = false;
  while (!reader.Empty()) {
    const auto field = reader.Next();
    switch (field.number) {
      case 1:
        Require(++nodes <= 4096);
        ValidateNode(Message(field), budget);
        break;
      case 2:
        Once(name_seen);
        Require(!Text(field).empty());
        break;
      case 5:
        Require(++tensors <= 1024);
        ValidateTensor(Message(field), budget);
        break;
      case 10:
        Once(doc_seen);
        static_cast<void>(Text(field));
        break;
      case 11:
        Require(++inputs <= 16);
        ValidateValueInfo(Message(field), budget, true);
        break;
      case 12:
        Require(++outputs <= 16);
        ValidateValueInfo(Message(field), budget, true);
        break;
      case 13:
        Require(++values <= 4096);
        ValidateValueInfo(Message(field), budget, false);
        break;
      case 16:
        ValidateMetadata(Message(field), budget);
        break;
      default:
        Reject();
    }
  }
  Require(name_seen && nodes != 0 && inputs != 0 && outputs != 0);
}

void ValidateOpset(std::string_view bytes, Budget& budget) {
  Reader reader(bytes, budget);
  bool domain_seen = false;
  bool version_seen = false;
  while (!reader.Empty()) {
    const auto field = reader.Next();
    if (field.number == 1) {
      Once(domain_seen);
      Require(Text(field).empty());
    } else if (field.number == 2) {
      Once(version_seen);
      Require(Integer(field) == 20);
    } else {
      Reject();
    }
  }
  Require(version_seen);
}

}  // namespace

void ValidateOnnxGraph(std::string_view bytes) {
  Require(!bytes.empty() && bytes.size() <= kMaxGraphBytes);
  Budget budget;
  Reader reader(bytes, budget);
  std::array<bool, 9> seen{};
  while (!reader.Empty()) {
    const auto field = reader.Next();
    if (field.number < seen.size()) {
      Once(seen[field.number]);
    }
    switch (field.number) {
      case 1:
        Require(Integer(field) == 10);
        break;
      case 2:
      case 3:
      case 6:
        static_cast<void>(Text(field));
        break;
      case 4:
        Require(Text(field).empty());
        break;
      case 5:
        static_cast<void>(Integer(field));
        break;
      case 7:
        ValidateGraph(Message(field), budget);
        break;
      case 8:
        ValidateOpset(Message(field), budget);
        break;
      case 14:
        ValidateMetadata(Message(field), budget);
        break;
      default:
        Reject();  // Includes functions, training_info and device configuration.
    }
  }
  Require(seen[1] && seen[7] && seen[8]);
}

}  // namespace ziliu::broker::detail
