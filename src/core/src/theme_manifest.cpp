#include "ziliu/core/theme_manifest.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace ziliu::core {
namespace {

constexpr std::size_t kMaximumJsonDepth = 32;
constexpr std::uint32_t kMaximumInset = 4096;
constexpr std::uint32_t kMinimumThemeFontSize = 8;
constexpr std::uint32_t kMaximumThemeFontSize = 96;
constexpr std::uint32_t kMinimumBaseDpi = 72;
constexpr std::uint32_t kMaximumBaseDpi = 384;

struct JsonNumber {
  std::string text;
};

struct JsonValue {
  using Array = std::vector<JsonValue>;
  using Object = std::map<std::string, JsonValue, std::less<>>;
  std::variant<std::nullptr_t, bool, JsonNumber, std::string, Array, Object> value;
};

bool IsValidUtf8(std::string_view text) {
  std::size_t index = 0;
  while (index < text.size()) {
    const auto first = static_cast<unsigned char>(text[index]);
    if (first <= 0x7F) {
      ++index;
      continue;
    }

    std::size_t continuation_count = 0;
    std::uint32_t code_point = 0;
    std::uint32_t minimum = 0;
    if ((first & 0xE0U) == 0xC0U) {
      continuation_count = 1;
      code_point = first & 0x1FU;
      minimum = 0x80;
    } else if ((first & 0xF0U) == 0xE0U) {
      continuation_count = 2;
      code_point = first & 0x0FU;
      minimum = 0x800;
    } else if ((first & 0xF8U) == 0xF0U) {
      continuation_count = 3;
      code_point = first & 0x07U;
      minimum = 0x10000;
    } else {
      return false;
    }

    if (index + continuation_count >= text.size()) {
      return false;
    }
    for (std::size_t offset = 1; offset <= continuation_count; ++offset) {
      const auto next = static_cast<unsigned char>(text[index + offset]);
      if ((next & 0xC0U) != 0x80U) {
        return false;
      }
      code_point = (code_point << 6U) | (next & 0x3FU);
    }
    if (code_point < minimum || code_point > 0x10FFFF ||
        (code_point >= 0xD800 && code_point <= 0xDFFF)) {
      return false;
    }
    index += continuation_count + 1;
  }
  return true;
}

void AppendUtf8(std::uint32_t code_point, std::string* output) {
  if (code_point <= 0x7F) {
    output->push_back(static_cast<char>(code_point));
  } else if (code_point <= 0x7FF) {
    output->push_back(static_cast<char>(0xC0U | (code_point >> 6U)));
    output->push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
  } else if (code_point <= 0xFFFF) {
    output->push_back(static_cast<char>(0xE0U | (code_point >> 12U)));
    output->push_back(static_cast<char>(0x80U | ((code_point >> 6U) & 0x3FU)));
    output->push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
  } else {
    output->push_back(static_cast<char>(0xF0U | (code_point >> 18U)));
    output->push_back(static_cast<char>(0x80U | ((code_point >> 12U) & 0x3FU)));
    output->push_back(static_cast<char>(0x80U | ((code_point >> 6U) & 0x3FU)));
    output->push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
  }
}

class JsonParser {
 public:
  explicit JsonParser(std::string_view text) : text_(text) {}

  std::optional<JsonValue> Parse(ThemeManifestIssue* issue) {
    issue_ = issue;
    SkipWhitespace();
    auto value = ParseValue(0);
    if (!value.has_value()) {
      return std::nullopt;
    }
    SkipWhitespace();
    if (position_ != text_.size()) {
      Fail("unexpected trailing content");
      return std::nullopt;
    }
    return value;
  }

 private:
  void SkipWhitespace() {
    while (position_ < text_.size()) {
      const char value = text_[position_];
      if (value != ' ' && value != '\t' && value != '\r' && value != '\n') {
        break;
      }
      ++position_;
    }
  }

  void Fail(std::string message) {
    if (failed_) {
      return;
    }
    failed_ = true;
    issue_->code = ThemeManifestIssueCode::kInvalidJson;
    issue_->offset = position_;
    issue_->path = "$";
    issue_->message = std::move(message);
  }

  bool Consume(char expected) {
    if (position_ >= text_.size() || text_[position_] != expected) {
      Fail(std::string("expected '") + expected + "'");
      return false;
    }
    ++position_;
    return true;
  }

  std::optional<JsonValue> ParseValue(std::size_t depth) {
    if (depth > kMaximumJsonDepth) {
      Fail("JSON nesting is too deep");
      return std::nullopt;
    }
    SkipWhitespace();
    if (position_ >= text_.size()) {
      Fail("unexpected end of JSON");
      return std::nullopt;
    }

    const char next = text_[position_];
    if (next == '{') {
      return ParseObject(depth);
    }
    if (next == '[') {
      return ParseArray(depth);
    }
    if (next == '"') {
      auto text = ParseString();
      if (!text.has_value()) {
        return std::nullopt;
      }
      return JsonValue{std::move(*text)};
    }
    if (next == 't' && ConsumeLiteral("true")) {
      return JsonValue{true};
    }
    if (next == 'f' && ConsumeLiteral("false")) {
      return JsonValue{false};
    }
    if (next == 'n' && ConsumeLiteral("null")) {
      return JsonValue{nullptr};
    }
    if (next == '-' || (next >= '0' && next <= '9')) {
      return ParseNumber();
    }
    Fail("unexpected token");
    return std::nullopt;
  }

  bool ConsumeLiteral(std::string_view literal) {
    if (text_.substr(position_, literal.size()) != literal) {
      Fail("invalid literal");
      return false;
    }
    position_ += literal.size();
    return true;
  }

  std::optional<JsonValue> ParseObject(std::size_t depth) {
    if (!Consume('{')) {
      return std::nullopt;
    }
    JsonValue::Object object;
    SkipWhitespace();
    if (position_ < text_.size() && text_[position_] == '}') {
      ++position_;
      return JsonValue{std::move(object)};
    }

    while (!failed_) {
      SkipWhitespace();
      auto key = ParseString();
      if (!key.has_value()) {
        return std::nullopt;
      }
      SkipWhitespace();
      if (!Consume(':')) {
        return std::nullopt;
      }
      auto value = ParseValue(depth + 1);
      if (!value.has_value()) {
        return std::nullopt;
      }
      if (!object.emplace(*key, std::move(*value)).second) {
        Fail("duplicate object property");
        return std::nullopt;
      }
      SkipWhitespace();
      if (position_ < text_.size() && text_[position_] == '}') {
        ++position_;
        return JsonValue{std::move(object)};
      }
      if (!Consume(',')) {
        return std::nullopt;
      }
    }
    return std::nullopt;
  }

  std::optional<JsonValue> ParseArray(std::size_t depth) {
    if (!Consume('[')) {
      return std::nullopt;
    }
    JsonValue::Array array;
    SkipWhitespace();
    if (position_ < text_.size() && text_[position_] == ']') {
      ++position_;
      return JsonValue{std::move(array)};
    }

    while (!failed_) {
      auto value = ParseValue(depth + 1);
      if (!value.has_value()) {
        return std::nullopt;
      }
      array.push_back(std::move(*value));
      SkipWhitespace();
      if (position_ < text_.size() && text_[position_] == ']') {
        ++position_;
        return JsonValue{std::move(array)};
      }
      if (!Consume(',')) {
        return std::nullopt;
      }
    }
    return std::nullopt;
  }

  std::optional<std::uint32_t> ParseHexQuad() {
    if (position_ + 4 > text_.size()) {
      Fail("incomplete Unicode escape");
      return std::nullopt;
    }
    std::uint32_t value = 0;
    for (std::size_t offset = 0; offset < 4; ++offset) {
      const char digit = text_[position_ + offset];
      value <<= 4U;
      if (digit >= '0' && digit <= '9') {
        value |= static_cast<std::uint32_t>(digit - '0');
      } else if (digit >= 'a' && digit <= 'f') {
        value |= static_cast<std::uint32_t>(digit - 'a' + 10);
      } else if (digit >= 'A' && digit <= 'F') {
        value |= static_cast<std::uint32_t>(digit - 'A' + 10);
      } else {
        Fail("invalid Unicode escape");
        return std::nullopt;
      }
    }
    position_ += 4;
    return value;
  }

  std::optional<std::string> ParseString() {
    if (!Consume('"')) {
      return std::nullopt;
    }
    std::string output;
    while (position_ < text_.size()) {
      const char value = text_[position_++];
      if (value == '"') {
        if (!IsValidUtf8(output)) {
          Fail("string is not valid UTF-8");
          return std::nullopt;
        }
        return output;
      }
      if (static_cast<unsigned char>(value) < 0x20U) {
        Fail("unescaped control character in string");
        return std::nullopt;
      }
      if (value != '\\') {
        output.push_back(value);
        continue;
      }
      if (position_ >= text_.size()) {
        Fail("incomplete escape sequence");
        return std::nullopt;
      }
      const char escaped = text_[position_++];
      switch (escaped) {
        case '"':
        case '\\':
        case '/':
          output.push_back(escaped);
          break;
        case 'b':
          output.push_back('\b');
          break;
        case 'f':
          output.push_back('\f');
          break;
        case 'n':
          output.push_back('\n');
          break;
        case 'r':
          output.push_back('\r');
          break;
        case 't':
          output.push_back('\t');
          break;
        case 'u': {
          auto code_point = ParseHexQuad();
          if (!code_point.has_value()) {
            return std::nullopt;
          }
          if (*code_point >= 0xD800 && *code_point <= 0xDBFF) {
            if (position_ + 2 > text_.size() || text_[position_] != '\\' ||
                text_[position_ + 1] != 'u') {
              Fail("high surrogate is missing its low surrogate");
              return std::nullopt;
            }
            position_ += 2;
            auto low = ParseHexQuad();
            if (!low.has_value() || *low < 0xDC00 || *low > 0xDFFF) {
              Fail("invalid low surrogate");
              return std::nullopt;
            }
            *code_point =
                0x10000U + ((*code_point - 0xD800U) << 10U) + (*low - 0xDC00U);
          } else if (*code_point >= 0xDC00 && *code_point <= 0xDFFF) {
            Fail("unexpected low surrogate");
            return std::nullopt;
          }
          AppendUtf8(*code_point, &output);
          break;
        }
        default:
          Fail("invalid escape sequence");
          return std::nullopt;
      }
    }
    Fail("unterminated string");
    return std::nullopt;
  }

  std::optional<JsonValue> ParseNumber() {
    const std::size_t start = position_;
    if (text_[position_] == '-') {
      ++position_;
      if (position_ >= text_.size()) {
        Fail("incomplete number");
        return std::nullopt;
      }
    }
    if (text_[position_] == '0') {
      ++position_;
      if (position_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[position_]))) {
        Fail("leading zero in number");
        return std::nullopt;
      }
    } else if (text_[position_] >= '1' && text_[position_] <= '9') {
      while (position_ < text_.size() &&
             std::isdigit(static_cast<unsigned char>(text_[position_]))) {
        ++position_;
      }
    } else {
      Fail("invalid number");
      return std::nullopt;
    }
    if (position_ < text_.size() && text_[position_] == '.') {
      ++position_;
      const std::size_t fraction_start = position_;
      while (position_ < text_.size() &&
             std::isdigit(static_cast<unsigned char>(text_[position_]))) {
        ++position_;
      }
      if (position_ == fraction_start) {
        Fail("number fraction has no digits");
        return std::nullopt;
      }
    }
    if (position_ < text_.size() && (text_[position_] == 'e' || text_[position_] == 'E')) {
      ++position_;
      if (position_ < text_.size() && (text_[position_] == '+' || text_[position_] == '-')) {
        ++position_;
      }
      const std::size_t exponent_start = position_;
      while (position_ < text_.size() &&
             std::isdigit(static_cast<unsigned char>(text_[position_]))) {
        ++position_;
      }
      if (position_ == exponent_start) {
        Fail("number exponent has no digits");
        return std::nullopt;
      }
    }
    return JsonValue{JsonNumber{std::string(text_.substr(start, position_ - start))}};
  }

  std::string_view text_;
  std::size_t position_ = 0;
  ThemeManifestIssue* issue_ = nullptr;
  bool failed_ = false;
};

const JsonValue* Find(const JsonValue::Object& object, std::string_view key) {
  const auto found = object.find(key);
  return found == object.end() ? nullptr : &found->second;
}

class ManifestReader {
 public:
  explicit ManifestReader(const JsonValue& root) : root_(root) {}

  ThemeManifestParseResult Read() {
    ThemeManifestParseResult result;
    const auto* root_object = std::get_if<JsonValue::Object>(&root_.value);
    if (root_object == nullptr) {
      AddIssue(ThemeManifestIssueCode::kInvalidProperty, "$", "manifest root must be an object");
      result.issues = std::move(issues_);
      return result;
    }

    ReadUnsigned(*root_object, "format_version", "$.format_version", true,
                 &result.manifest.format_version);
    ReadString(*root_object, "id", "$.id", true, &result.manifest.id);
    ReadString(*root_object, "name", "$.name", true, &result.manifest.name);
    ReadString(*root_object, "author", "$.author", true, &result.manifest.author);
    ReadString(*root_object, "version", "$.version", true, &result.manifest.version);
    ReadString(*root_object, "license", "$.license", true, &result.manifest.license);
    ReadString(*root_object, "description", "$.description", false,
               &result.manifest.description);
    ReadString(*root_object, "homepage", "$.homepage", false, &result.manifest.homepage);
    ReadString(*root_object, "preview", "$.preview", false, &result.manifest.preview_asset);
    ReadString(*root_object, "source_format", "$.source_format", false,
               &result.manifest.source_format);
    ReadUnsigned(*root_object, "base_dpi", "$.base_dpi", false, &result.manifest.base_dpi);

    const JsonValue* appearances_value = Find(*root_object, "appearances");
    if (appearances_value == nullptr) {
      AddIssue(ThemeManifestIssueCode::kMissingProperty, "$.appearances",
               "required property is missing");
    } else if (const auto* appearances =
                   std::get_if<JsonValue::Object>(&appearances_value->value);
               appearances != nullptr) {
      const JsonValue* light_value = Find(*appearances, "light");
      if (light_value == nullptr) {
        AddIssue(ThemeManifestIssueCode::kMissingProperty, "$.appearances.light",
                 "required property is missing");
      } else {
        ReadAppearance(*light_value, "$.appearances.light", &result.manifest.light);
      }
      const JsonValue* dark_value = Find(*appearances, "dark");
      if (dark_value != nullptr) {
        ThemeAppearance dark;
        ReadAppearance(*dark_value, "$.appearances.dark", &dark);
        result.manifest.dark = std::move(dark);
      }
    } else {
      AddIssue(ThemeManifestIssueCode::kInvalidProperty, "$.appearances",
               "property must be an object");
    }

    result.issues = std::move(issues_);
    return result;
  }

 private:
  void AddIssue(ThemeManifestIssueCode code, std::string path, std::string message) {
    issues_.push_back({code, 0, std::move(path), std::move(message)});
  }

  void ReadString(const JsonValue::Object& object, std::string_view key, std::string path,
                  bool required, std::string* output) {
    const JsonValue* value = Find(object, key);
    if (value == nullptr) {
      if (required) {
        AddIssue(ThemeManifestIssueCode::kMissingProperty, std::move(path),
                 "required property is missing");
      }
      return;
    }
    const auto* text = std::get_if<std::string>(&value->value);
    if (text == nullptr) {
      AddIssue(ThemeManifestIssueCode::kInvalidProperty, std::move(path),
               "property must be a string");
      return;
    }
    *output = *text;
  }

  void ReadUnsigned(const JsonValue::Object& object, std::string_view key, std::string path,
                    bool required, std::uint32_t* output) {
    const JsonValue* value = Find(object, key);
    if (value == nullptr) {
      if (required) {
        AddIssue(ThemeManifestIssueCode::kMissingProperty, std::move(path),
                 "required property is missing");
      }
      return;
    }
    const auto* number = std::get_if<JsonNumber>(&value->value);
    if (number == nullptr) {
      AddIssue(ThemeManifestIssueCode::kInvalidProperty, std::move(path),
               "property must be an unsigned integer");
      return;
    }
    std::uint32_t parsed = 0;
    const auto conversion =
        std::from_chars(number->text.data(), number->text.data() + number->text.size(), parsed);
    if (conversion.ec != std::errc{} ||
        conversion.ptr != number->text.data() + number->text.size()) {
      AddIssue(ThemeManifestIssueCode::kInvalidProperty, std::move(path),
               "property must be an unsigned integer");
      return;
    }
    *output = parsed;
  }

  void ReadColor(const JsonValue::Object& object, std::string_view key, std::string path,
                 std::uint32_t* output) {
    std::string text;
    ReadString(object, key, path, true, &text);
    if (text.empty()) {
      return;
    }
    if (text.size() != 7 && text.size() != 9) {
      AddIssue(ThemeManifestIssueCode::kInvalidProperty, std::move(path),
               "color must use #RRGGBB or #RRGGBBAA");
      return;
    }
    if (text.front() != '#') {
      AddIssue(ThemeManifestIssueCode::kInvalidProperty, std::move(path),
               "color must start with '#'");
      return;
    }
    std::uint32_t rgba = 0;
    const auto conversion =
        std::from_chars(text.data() + 1, text.data() + text.size(), rgba, 16);
    if (conversion.ec != std::errc{} || conversion.ptr != text.data() + text.size()) {
      AddIssue(ThemeManifestIssueCode::kInvalidProperty, std::move(path),
               "color contains non-hexadecimal digits");
      return;
    }
    if (text.size() == 7) {
      *output = 0xFF000000U | rgba;
    } else {
      const std::uint32_t rgb = rgba >> 8U;
      const std::uint32_t alpha = rgba & 0xFFU;
      *output = (alpha << 24U) | rgb;
    }
  }

  void ReadOptionalColor(const JsonValue::Object& object, std::string_view key,
                         std::string path, std::optional<std::uint32_t>* output) {
    if (Find(object, key) == nullptr) {
      return;
    }
    std::uint32_t color = 0;
    const std::size_t issue_count = issues_.size();
    ReadColor(object, key, std::move(path), &color);
    if (issues_.size() == issue_count) {
      *output = color;
    }
  }

  void ReadInsets(const JsonValue::Object& object, std::string_view key, std::string path,
                  ThemeInsets* output) {
    const JsonValue* value = Find(object, key);
    if (value == nullptr) {
      return;
    }
    const auto* array = std::get_if<JsonValue::Array>(&value->value);
    if (array == nullptr || array->size() != 4) {
      AddIssue(ThemeManifestIssueCode::kInvalidProperty, std::move(path),
               "insets must be [left, top, right, bottom]");
      return;
    }
    std::uint32_t values[4]{};
    for (std::size_t index = 0; index < 4; ++index) {
      const auto* number = std::get_if<JsonNumber>(&(*array)[index].value);
      if (number == nullptr) {
        AddIssue(ThemeManifestIssueCode::kInvalidProperty, path,
                 "each inset must be an unsigned integer");
        return;
      }
      const auto conversion =
          std::from_chars(number->text.data(), number->text.data() + number->text.size(),
                          values[index]);
      if (conversion.ec != std::errc{} ||
          conversion.ptr != number->text.data() + number->text.size()) {
        AddIssue(ThemeManifestIssueCode::kInvalidProperty, path,
                 "each inset must be an unsigned integer");
        return;
      }
    }
    *output = {values[0], values[1], values[2], values[3]};
  }

  void ReadOverlayAlignment(const JsonValue::Object& object, std::string_view key,
                            std::string path,
                            std::array<std::int32_t, 10>* output) {
    const JsonValue* value = Find(object, key);
    if (value == nullptr) {
      AddIssue(ThemeManifestIssueCode::kMissingProperty, std::move(path),
               "required property is missing");
      return;
    }
    const auto* array = std::get_if<JsonValue::Array>(&value->value);
    if (array == nullptr || array->size() != output->size()) {
      AddIssue(ThemeManifestIssueCode::kInvalidProperty, std::move(path),
               "overlay align must contain exactly 10 signed integers");
      return;
    }
    for (std::size_t index = 0; index < output->size(); ++index) {
      const auto* number = std::get_if<JsonNumber>(&(*array)[index].value);
      if (number == nullptr) {
        AddIssue(ThemeManifestIssueCode::kInvalidProperty, path,
                 "each overlay align value must be a signed integer");
        return;
      }
      std::int32_t parsed = 0;
      const auto conversion =
          std::from_chars(number->text.data(),
                          number->text.data() + number->text.size(), parsed);
      if (conversion.ec != std::errc{} ||
          conversion.ptr != number->text.data() + number->text.size()) {
        AddIssue(ThemeManifestIssueCode::kInvalidProperty, path,
                 "each overlay align value must be a signed 32-bit integer");
        return;
      }
      (*output)[index] = parsed;
    }
  }

  void ReadImage(const JsonValue& value, std::string path, ThemeImage* output) {
    const auto* object = std::get_if<JsonValue::Object>(&value.value);
    if (object == nullptr) {
      AddIssue(ThemeManifestIssueCode::kInvalidProperty, std::move(path),
               "image must be an object");
      return;
    }
    ReadString(*object, "asset", path + ".asset", true, &output->asset);
    ReadInsets(*object, "stretch", path + ".stretch", &output->stretch);
    if (const JsonValue* layout_value = Find(*object, "layout"); layout_value != nullptr) {
      const auto* layout = std::get_if<JsonValue::Object>(&layout_value->value);
      if (layout == nullptr) {
        AddIssue(ThemeManifestIssueCode::kInvalidProperty, path + ".layout",
                 "layout must be an object");
      } else {
        ReadImageLayout(*layout, "horizontal", path + ".layout.horizontal",
                        &output->horizontal_layout);
        ReadImageLayout(*layout, "vertical", path + ".layout.vertical",
                        &output->vertical_layout);
      }
    }
  }

  void ReadImageLayout(const JsonValue::Object& object, std::string_view key,
                       std::string path, ThemeImageLayout* output) {
    std::string layout;
    ReadString(object, key, path, false, &layout);
    if (layout.empty()) {
      return;
    }
    if (layout == "stretch") {
      *output = ThemeImageLayout::kStretch;
    } else if (layout == "tile") {
      *output = ThemeImageLayout::kTile;
    } else if (layout == "fixed") {
      *output = ThemeImageLayout::kFixed;
    } else {
      AddIssue(ThemeManifestIssueCode::kInvalidProperty, std::move(path),
               "layout must be 'stretch', 'tile' or 'fixed'");
    }
  }

  void ReadOptionalInsets(const JsonValue::Object& object, std::string_view key,
                          std::string path, std::optional<ThemeInsets>* output) {
    if (Find(object, key) == nullptr) {
      return;
    }
    ThemeInsets insets;
    const std::size_t issue_count = issues_.size();
    ReadInsets(object, key, std::move(path), &insets);
    if (issues_.size() == issue_count) {
      *output = insets;
    }
  }

  void ReadSeparator(const JsonValue& value, std::string path, ThemeSeparator* output) {
    const auto* object = std::get_if<JsonValue::Object>(&value.value);
    if (object == nullptr) {
      AddIssue(ThemeManifestIssueCode::kInvalidProperty, std::move(path),
               "separator must be an object");
      return;
    }
    ReadOptionalColor(*object, "color", path + ".color", &output->color);
    ReadString(*object, "asset", path + ".asset", false, &output->asset);
    ReadUnsigned(*object, "left", path + ".left", false, &output->left);
    ReadUnsigned(*object, "right", path + ".right", false, &output->right);
    ReadUnsigned(*object, "thickness", path + ".thickness", false, &output->thickness);
  }

  void ReadOverlay(const JsonValue& value, std::string path,
                   ThemeOverlay* output) {
    const auto* object = std::get_if<JsonValue::Object>(&value.value);
    if (object == nullptr) {
      AddIssue(ThemeManifestIssueCode::kInvalidProperty, std::move(path),
               "overlay must be an object");
      return;
    }
    ReadString(*object, "asset", path + ".asset", true, &output->asset);
    ReadUnsigned(*object, "custom_index", path + ".custom_index", true,
                 &output->custom_index);
    ReadUnsigned(*object, "draw_order", path + ".draw_order", true,
                 &output->draw_order);
    ReadOverlayAlignment(*object, "align", path + ".align", &output->align);
  }

  void ReadButton(const JsonValue& value, std::string path, ThemeButtonImages* output) {
    const auto* object = std::get_if<JsonValue::Object>(&value.value);
    if (object == nullptr) {
      AddIssue(ThemeManifestIssueCode::kInvalidProperty, std::move(path),
               "button must be an object");
      return;
    }
    ReadString(*object, "normal", path + ".normal", true, &output->normal);
    ReadString(*object, "hover", path + ".hover", false, &output->hover);
    ReadString(*object, "pressed", path + ".pressed", false, &output->pressed);
  }

  void ReadOptionalButton(const JsonValue::Object& buttons, std::string_view key,
                          const std::string& path, std::optional<ThemeButtonImages>* output) {
    const JsonValue* value = Find(buttons, key);
    if (value == nullptr) {
      return;
    }
    ThemeButtonImages images;
    ReadButton(*value, path + "." + std::string(key), &images);
    *output = std::move(images);
  }

  void ReadSurface(const JsonValue& value, std::string path, ThemeSurface* output) {
    const auto* object = std::get_if<JsonValue::Object>(&value.value);
    if (object == nullptr) {
      AddIssue(ThemeManifestIssueCode::kInvalidProperty, std::move(path),
               "surface must be an object");
      return;
    }
    if (const JsonValue* background = Find(*object, "background"); background != nullptr) {
      ThemeImage image;
      ReadImage(*background, path + ".background", &image);
      output->background = std::move(image);
    }
    if (const JsonValue* overlays_value = Find(*object, "overlays");
        overlays_value != nullptr) {
      const auto* overlays =
          std::get_if<JsonValue::Array>(&overlays_value->value);
      if (overlays == nullptr) {
        AddIssue(ThemeManifestIssueCode::kInvalidProperty, path + ".overlays",
                 "overlays must be an array");
      } else if (overlays->size() > kMaximumThemeSurfaceOverlays) {
        AddIssue(ThemeManifestIssueCode::kInvalidProperty, path + ".overlays",
                 "a surface may contain at most 128 overlays");
      } else {
        output->overlays.reserve(overlays->size());
        for (std::size_t index = 0; index < overlays->size(); ++index) {
          ThemeOverlay overlay;
          ReadOverlay((*overlays)[index],
                      path + ".overlays[" + std::to_string(index) + "]",
                      &overlay);
          output->overlays.push_back(std::move(overlay));
        }
      }
    }
    if (const JsonValue* content_value = Find(*object, "content"); content_value != nullptr) {
      const auto* content = std::get_if<JsonValue::Object>(&content_value->value);
      if (content == nullptr) {
        AddIssue(ThemeManifestIssueCode::kInvalidProperty, path + ".content",
                 "content must be an object");
      } else {
        ReadOptionalInsets(*content, "preedit", path + ".content.preedit",
                           &output->preedit_insets);
        ReadOptionalInsets(*content, "candidates", path + ".content.candidates",
                           &output->candidate_insets);
      }
    }
    if (const JsonValue* separator_value = Find(*object, "separator");
        separator_value != nullptr) {
      ThemeSeparator separator;
      ReadSeparator(*separator_value, path + ".separator", &separator);
      output->separator = std::move(separator);
    }
    if (const JsonValue* buttons_value = Find(*object, "buttons"); buttons_value != nullptr) {
      const auto* buttons = std::get_if<JsonValue::Object>(&buttons_value->value);
      if (buttons == nullptr) {
        AddIssue(ThemeManifestIssueCode::kInvalidProperty, path + ".buttons",
                 "buttons must be an object");
      } else {
        ReadOptionalButton(*buttons, "previous", path + ".buttons", &output->previous_button);
        ReadOptionalButton(*buttons, "next", path + ".buttons", &output->next_button);
        ReadOptionalButton(*buttons, "expand", path + ".buttons", &output->expand_button);
        ReadOptionalButton(*buttons, "collapse", path + ".buttons", &output->collapse_button);
        ReadOptionalButton(*buttons, "menu", path + ".buttons", &output->menu_button);
      }
    }
  }

  void ReadPalette(const JsonValue& value, std::string path, ThemePalette* output) {
    const auto* object = std::get_if<JsonValue::Object>(&value.value);
    if (object == nullptr) {
      AddIssue(ThemeManifestIssueCode::kInvalidProperty, std::move(path),
               "palette must be an object");
      return;
    }
    ReadColor(*object, "preedit_text", path + ".preedit_text", &output->preedit_text);
    ReadColor(*object, "candidate_text", path + ".candidate_text", &output->candidate_text);
    ReadColor(*object, "highlighted_candidate_text", path + ".highlighted_candidate_text",
              &output->highlighted_candidate_text);
    ReadColor(*object, "background", path + ".background", &output->background);
    ReadColor(*object, "highlighted_background", path + ".highlighted_background",
              &output->highlighted_background);
    ReadColor(*object, "muted_text", path + ".muted_text", &output->muted_text);
    ReadColor(*object, "separator", path + ".separator", &output->separator);
  }

  void ReadTypography(const JsonValue& value, std::string path, ThemeTypography* output) {
    const auto* object = std::get_if<JsonValue::Object>(&value.value);
    if (object == nullptr) {
      AddIssue(ThemeManifestIssueCode::kInvalidProperty, std::move(path),
               "typography must be an object");
      return;
    }
    ReadString(*object, "chinese_font_family", path + ".chinese_font_family", true,
               &output->chinese_font_family);
    ReadString(*object, "english_font_family", path + ".english_font_family", true,
               &output->english_font_family);
    ReadUnsigned(*object, "font_size", path + ".font_size", true, &output->font_size);
  }

  void ReadAppearance(const JsonValue& value, std::string path, ThemeAppearance* output) {
    const auto* object = std::get_if<JsonValue::Object>(&value.value);
    if (object == nullptr) {
      AddIssue(ThemeManifestIssueCode::kInvalidProperty, std::move(path),
               "appearance must be an object");
      return;
    }
    if (const JsonValue* palette = Find(*object, "palette"); palette != nullptr) {
      ReadPalette(*palette, path + ".palette", &output->palette);
    } else {
      AddIssue(ThemeManifestIssueCode::kMissingProperty, path + ".palette",
               "required property is missing");
    }
    if (const JsonValue* typography = Find(*object, "typography"); typography != nullptr) {
      ReadTypography(*typography, path + ".typography", &output->typography);
    } else {
      AddIssue(ThemeManifestIssueCode::kMissingProperty, path + ".typography",
               "required property is missing");
    }
    if (const JsonValue* surfaces_value = Find(*object, "surfaces"); surfaces_value != nullptr) {
      const auto* surfaces = std::get_if<JsonValue::Object>(&surfaces_value->value);
      if (surfaces == nullptr) {
        AddIssue(ThemeManifestIssueCode::kInvalidProperty, path + ".surfaces",
                 "surfaces must be an object");
      } else {
        if (const JsonValue* horizontal = Find(*surfaces, "horizontal");
            horizontal != nullptr) {
          ReadSurface(*horizontal, path + ".surfaces.horizontal", &output->horizontal);
        }
        if (const JsonValue* vertical = Find(*surfaces, "vertical"); vertical != nullptr) {
          ReadSurface(*vertical, path + ".surfaces.vertical", &output->vertical);
        }
      }
    }
  }

  const JsonValue& root_;
  std::vector<ThemeManifestIssue> issues_;
};

bool HasControlCharacters(std::string_view text) {
  return std::any_of(text.begin(), text.end(), [](char value) {
    return static_cast<unsigned char>(value) < 0x20U;
  });
}

bool IsValidThemeId(std::string_view id) {
  if (id.empty() || id.size() > 128 ||
      !std::isalnum(static_cast<unsigned char>(id.front()))) {
    return false;
  }
  return std::all_of(id.begin(), id.end(), [](char value) {
    const auto unsigned_value = static_cast<unsigned char>(value);
    return std::islower(unsigned_value) || std::isdigit(unsigned_value) || value == '.' ||
           value == '_' || value == '-';
  });
}

bool HasSupportedImageExtension(std::string_view path) {
  const std::size_t dot = path.find_last_of('.');
  if (dot == std::string_view::npos) {
    return false;
  }
  std::string extension(path.substr(dot));
  std::transform(extension.begin(), extension.end(), extension.begin(), [](char value) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
  });
  return extension == ".png" || extension == ".apng";
}

void AddValidationIssue(std::vector<ThemeManifestIssue>* issues, ThemeManifestIssueCode code,
                        std::string path, std::string message) {
  issues->push_back({code, 0, std::move(path), std::move(message)});
}

void ValidateText(std::vector<ThemeManifestIssue>* issues, std::string_view value,
                  std::string path, std::size_t maximum_length, bool required) {
  if (required && value.empty()) {
    AddValidationIssue(issues, ThemeManifestIssueCode::kInvalidProperty, std::move(path),
                       "value must not be empty");
    return;
  }
  if (value.size() > maximum_length) {
    AddValidationIssue(issues, ThemeManifestIssueCode::kInvalidProperty, std::move(path),
                       "value is too long");
    return;
  }
  if (!IsValidUtf8(value) || HasControlCharacters(value)) {
    AddValidationIssue(issues, ThemeManifestIssueCode::kInvalidProperty, std::move(path),
                       "value must be valid UTF-8 without control characters");
  }
}

void ValidateAsset(std::vector<ThemeManifestIssue>* issues, std::string_view asset,
                   std::string path, bool required) {
  if (asset.empty()) {
    if (required) {
      AddValidationIssue(issues, ThemeManifestIssueCode::kUnsafeAssetPath, std::move(path),
                         "asset path must not be empty");
    }
    return;
  }
  if (!IsSafeThemeAssetPath(asset)) {
    AddValidationIssue(issues, ThemeManifestIssueCode::kUnsafeAssetPath, std::move(path),
                       "asset path must be a safe relative POSIX path");
    return;
  }
  if (!HasSupportedImageExtension(asset)) {
    AddValidationIssue(issues, ThemeManifestIssueCode::kInvalidProperty, std::move(path),
                       "theme images must use PNG or APNG");
  }
}

void ValidateInsets(std::vector<ThemeManifestIssue>* issues, const ThemeInsets& insets,
                    std::string path) {
  if (insets.left > kMaximumInset || insets.top > kMaximumInset ||
      insets.right > kMaximumInset || insets.bottom > kMaximumInset) {
    AddValidationIssue(issues, ThemeManifestIssueCode::kInvalidProperty, std::move(path),
                       "each inset must be at most 4096");
  }
}

void ValidateImageLayout(std::vector<ThemeManifestIssue>* issues,
                         ThemeImageLayout layout, std::string path) {
  switch (layout) {
    case ThemeImageLayout::kStretch:
    case ThemeImageLayout::kTile:
    case ThemeImageLayout::kFixed:
      return;
  }
  AddValidationIssue(issues, ThemeManifestIssueCode::kInvalidProperty,
                     std::move(path),
                     "layout must be stretch, tile or fixed");
}

void ValidateButton(std::vector<ThemeManifestIssue>* issues, const ThemeButtonImages& button,
                    std::string path) {
  ValidateAsset(issues, button.normal, path + ".normal", true);
  ValidateAsset(issues, button.hover, path + ".hover", false);
  ValidateAsset(issues, button.pressed, path + ".pressed", false);
}

void ValidateSeparator(std::vector<ThemeManifestIssue>* issues,
                       const ThemeSeparator& separator, std::string path) {
  ValidateAsset(issues, separator.asset, path + ".asset", false);
  if (separator.left > kMaximumInset || separator.right > kMaximumInset) {
    AddValidationIssue(issues, ThemeManifestIssueCode::kInvalidProperty, path,
                       "separator left and right margins must be at most 4096");
  }
  if (separator.thickness == 0 || separator.thickness > kMaximumInset) {
    AddValidationIssue(issues, ThemeManifestIssueCode::kInvalidProperty,
                       path + ".thickness",
                       "separator thickness must be between 1 and 4096");
  }
}

void ValidateOverlay(std::vector<ThemeManifestIssue>* issues,
                     const ThemeOverlay& overlay, std::string path) {
  ValidateAsset(issues, overlay.asset, path + ".asset", true);
}

void ValidateSurface(std::vector<ThemeManifestIssue>* issues, const ThemeSurface& surface,
                     std::string path) {
  if (surface.background.has_value()) {
    ValidateAsset(issues, surface.background->asset, path + ".background.asset", true);
    ValidateInsets(issues, surface.background->stretch, path + ".background.stretch");
    ValidateImageLayout(issues, surface.background->horizontal_layout,
                        path + ".background.layout.horizontal");
    ValidateImageLayout(issues, surface.background->vertical_layout,
                        path + ".background.layout.vertical");
  }
  if (surface.overlays.size() > kMaximumThemeSurfaceOverlays) {
    AddValidationIssue(issues, ThemeManifestIssueCode::kInvalidProperty,
                       path + ".overlays",
                       "a surface may contain at most 128 overlays");
  }
  for (std::size_t index = 0; index < surface.overlays.size(); ++index) {
    ValidateOverlay(issues, surface.overlays[index],
                    path + ".overlays[" + std::to_string(index) + "]");
  }
  if (surface.preedit_insets.has_value()) {
    ValidateInsets(issues, *surface.preedit_insets, path + ".content.preedit");
  }
  if (surface.candidate_insets.has_value()) {
    ValidateInsets(issues, *surface.candidate_insets, path + ".content.candidates");
  }
  if (surface.separator.has_value()) {
    ValidateSeparator(issues, *surface.separator, path + ".separator");
  }
  if (surface.previous_button.has_value()) {
    ValidateButton(issues, *surface.previous_button, path + ".buttons.previous");
  }
  if (surface.next_button.has_value()) {
    ValidateButton(issues, *surface.next_button, path + ".buttons.next");
  }
  if (surface.expand_button.has_value()) {
    ValidateButton(issues, *surface.expand_button, path + ".buttons.expand");
  }
  if (surface.collapse_button.has_value()) {
    ValidateButton(issues, *surface.collapse_button, path + ".buttons.collapse");
  }
  if (surface.menu_button.has_value()) {
    ValidateButton(issues, *surface.menu_button, path + ".buttons.menu");
  }
}

void ValidateAppearance(std::vector<ThemeManifestIssue>* issues,
                        const ThemeAppearance& appearance, std::string path) {
  ValidateText(issues, appearance.typography.chinese_font_family,
               path + ".typography.chinese_font_family", 128, true);
  ValidateText(issues, appearance.typography.english_font_family,
               path + ".typography.english_font_family", 128, true);
  if (appearance.typography.font_size < kMinimumThemeFontSize ||
      appearance.typography.font_size > kMaximumThemeFontSize) {
    AddValidationIssue(issues, ThemeManifestIssueCode::kInvalidProperty,
                       path + ".typography.font_size", "font size must be between 8 and 96");
  }
  ValidateSurface(issues, appearance.horizontal, path + ".surfaces.horizontal");
  ValidateSurface(issues, appearance.vertical, path + ".surfaces.vertical");
}

std::string EscapeJsonString(std::string_view text) {
  constexpr char kHex[] = "0123456789ABCDEF";
  std::string escaped;
  escaped.reserve(text.size() + 2);
  escaped.push_back('"');
  for (const char value : text) {
    switch (value) {
      case '"':
        escaped += "\\\"";
        break;
      case '\\':
        escaped += "\\\\";
        break;
      case '\b':
        escaped += "\\b";
        break;
      case '\f':
        escaped += "\\f";
        break;
      case '\n':
        escaped += "\\n";
        break;
      case '\r':
        escaped += "\\r";
        break;
      case '\t':
        escaped += "\\t";
        break;
      default: {
        const auto unsigned_value = static_cast<unsigned char>(value);
        if (unsigned_value < 0x20U) {
          escaped += "\\u00";
          escaped.push_back(kHex[(unsigned_value >> 4U) & 0x0FU]);
          escaped.push_back(kHex[unsigned_value & 0x0FU]);
        } else {
          escaped.push_back(value);
        }
        break;
      }
    }
  }
  escaped.push_back('"');
  return escaped;
}

std::string SerializeColor(std::uint32_t argb) {
  constexpr char kHex[] = "0123456789ABCDEF";
  const std::uint32_t alpha = (argb >> 24U) & 0xFFU;
  const std::uint32_t red = (argb >> 16U) & 0xFFU;
  const std::uint32_t green = (argb >> 8U) & 0xFFU;
  const std::uint32_t blue = argb & 0xFFU;
  std::string result = "#000000";
  result[1] = kHex[(red >> 4U) & 0x0FU];
  result[2] = kHex[red & 0x0FU];
  result[3] = kHex[(green >> 4U) & 0x0FU];
  result[4] = kHex[green & 0x0FU];
  result[5] = kHex[(blue >> 4U) & 0x0FU];
  result[6] = kHex[blue & 0x0FU];
  if (alpha != 0xFFU) {
    result.push_back(kHex[(alpha >> 4U) & 0x0FU]);
    result.push_back(kHex[alpha & 0x0FU]);
  }
  return result;
}

std::string_view SerializeImageLayout(ThemeImageLayout layout) {
  switch (layout) {
    case ThemeImageLayout::kStretch:
      return "stretch";
    case ThemeImageLayout::kTile:
      return "tile";
    case ThemeImageLayout::kFixed:
      return "fixed";
  }
  return "stretch";
}

class JsonWriter {
 public:
  void BeginDocument() {
    output_.push_back('{');
    first_property_.push_back(true);
  }

  std::string FinishDocument() {
    EndObject();
    output_.push_back('\n');
    return std::move(output_);
  }

  void StringProperty(std::string_view key, std::string_view value) {
    PropertyPrefix(key);
    output_ += EscapeJsonString(value);
  }

  void UnsignedProperty(std::string_view key, std::uint32_t value) {
    PropertyPrefix(key);
    output_ += std::to_string(value);
  }

  void InsetsProperty(std::string_view key, const ThemeInsets& insets) {
    PropertyPrefix(key);
    output_ += "[" + std::to_string(insets.left) + ", " + std::to_string(insets.top) + ", " +
               std::to_string(insets.right) + ", " + std::to_string(insets.bottom) + "]";
  }

  void OverlayAlignmentProperty(
      std::string_view key, const std::array<std::int32_t, 10>& alignment) {
    PropertyPrefix(key);
    output_.push_back('[');
    for (std::size_t index = 0; index < alignment.size(); ++index) {
      if (index != 0) {
        output_ += ", ";
      }
      output_ += std::to_string(alignment[index]);
    }
    output_.push_back(']');
  }

  void BeginObjectProperty(std::string_view key) {
    PropertyPrefix(key);
    output_.push_back('{');
    first_property_.push_back(true);
  }

  void BeginArrayProperty(std::string_view key) {
    PropertyPrefix(key);
    output_.push_back('[');
    first_property_.push_back(true);
  }

  void BeginObjectElement() {
    ElementPrefix();
    output_.push_back('{');
    first_property_.push_back(true);
  }

  void EndObject() {
    const bool empty = first_property_.back();
    first_property_.pop_back();
    if (!empty) {
      output_.push_back('\n');
      AppendIndent(first_property_.size());
    }
    output_.push_back('}');
  }

  void EndArray() {
    const bool empty = first_property_.back();
    first_property_.pop_back();
    if (!empty) {
      output_.push_back('\n');
      AppendIndent(first_property_.size());
    }
    output_.push_back(']');
  }

 private:
  void PropertyPrefix(std::string_view key) {
    if (!first_property_.back()) {
      output_.push_back(',');
    }
    first_property_.back() = false;
    output_.push_back('\n');
    AppendIndent(first_property_.size());
    output_ += EscapeJsonString(key);
    output_ += ": ";
  }

  void ElementPrefix() {
    if (!first_property_.back()) {
      output_.push_back(',');
    }
    first_property_.back() = false;
    output_.push_back('\n');
    AppendIndent(first_property_.size());
  }

  void AppendIndent(std::size_t depth) { output_.append(depth * 2, ' '); }

  std::string output_;
  std::vector<bool> first_property_;
};

void WriteButton(JsonWriter* writer, std::string_view key, const ThemeButtonImages& button) {
  writer->BeginObjectProperty(key);
  writer->StringProperty("normal", button.normal);
  if (!button.hover.empty()) {
    writer->StringProperty("hover", button.hover);
  }
  if (!button.pressed.empty()) {
    writer->StringProperty("pressed", button.pressed);
  }
  writer->EndObject();
}

void WriteOverlay(JsonWriter* writer, const ThemeOverlay& overlay) {
  writer->BeginObjectElement();
  writer->StringProperty("asset", overlay.asset);
  writer->UnsignedProperty("custom_index", overlay.custom_index);
  writer->UnsignedProperty("draw_order", overlay.draw_order);
  writer->OverlayAlignmentProperty("align", overlay.align);
  writer->EndObject();
}

void WriteSurface(JsonWriter* writer, std::string_view key, const ThemeSurface& surface) {
  writer->BeginObjectProperty(key);
  if (surface.background.has_value()) {
    writer->BeginObjectProperty("background");
    writer->StringProperty("asset", surface.background->asset);
    writer->BeginObjectProperty("layout");
    writer->StringProperty(
        "horizontal",
        SerializeImageLayout(surface.background->horizontal_layout));
    writer->StringProperty(
        "vertical",
        SerializeImageLayout(surface.background->vertical_layout));
    writer->EndObject();
    writer->InsetsProperty("stretch", surface.background->stretch);
    writer->EndObject();
  }
  if (!surface.overlays.empty()) {
    writer->BeginArrayProperty("overlays");
    for (const ThemeOverlay& overlay : surface.overlays) {
      WriteOverlay(writer, overlay);
    }
    writer->EndArray();
  }
  if (surface.preedit_insets.has_value() || surface.candidate_insets.has_value()) {
    writer->BeginObjectProperty("content");
    if (surface.preedit_insets.has_value()) {
      writer->InsetsProperty("preedit", *surface.preedit_insets);
    }
    if (surface.candidate_insets.has_value()) {
      writer->InsetsProperty("candidates", *surface.candidate_insets);
    }
    writer->EndObject();
  }
  if (surface.separator.has_value()) {
    writer->BeginObjectProperty("separator");
    if (surface.separator->color.has_value()) {
      writer->StringProperty("color", SerializeColor(*surface.separator->color));
    }
    if (!surface.separator->asset.empty()) {
      writer->StringProperty("asset", surface.separator->asset);
    }
    writer->UnsignedProperty("left", surface.separator->left);
    writer->UnsignedProperty("right", surface.separator->right);
    writer->UnsignedProperty("thickness", surface.separator->thickness);
    writer->EndObject();
  }
  const bool has_buttons =
      surface.previous_button.has_value() || surface.next_button.has_value() ||
      surface.expand_button.has_value() || surface.collapse_button.has_value() ||
      surface.menu_button.has_value();
  if (has_buttons) {
    writer->BeginObjectProperty("buttons");
    if (surface.previous_button.has_value()) {
      WriteButton(writer, "previous", *surface.previous_button);
    }
    if (surface.next_button.has_value()) {
      WriteButton(writer, "next", *surface.next_button);
    }
    if (surface.expand_button.has_value()) {
      WriteButton(writer, "expand", *surface.expand_button);
    }
    if (surface.collapse_button.has_value()) {
      WriteButton(writer, "collapse", *surface.collapse_button);
    }
    if (surface.menu_button.has_value()) {
      WriteButton(writer, "menu", *surface.menu_button);
    }
    writer->EndObject();
  }
  writer->EndObject();
}

void WriteAppearance(JsonWriter* writer, std::string_view key,
                     const ThemeAppearance& appearance) {
  writer->BeginObjectProperty(key);
  writer->BeginObjectProperty("palette");
  writer->StringProperty("preedit_text", SerializeColor(appearance.palette.preedit_text));
  writer->StringProperty("candidate_text", SerializeColor(appearance.palette.candidate_text));
  writer->StringProperty("highlighted_candidate_text",
                         SerializeColor(appearance.palette.highlighted_candidate_text));
  writer->StringProperty("background", SerializeColor(appearance.palette.background));
  writer->StringProperty("highlighted_background",
                         SerializeColor(appearance.palette.highlighted_background));
  writer->StringProperty("muted_text", SerializeColor(appearance.palette.muted_text));
  writer->StringProperty("separator", SerializeColor(appearance.palette.separator));
  writer->EndObject();

  writer->BeginObjectProperty("typography");
  writer->StringProperty("chinese_font_family", appearance.typography.chinese_font_family);
  writer->StringProperty("english_font_family", appearance.typography.english_font_family);
  writer->UnsignedProperty("font_size", appearance.typography.font_size);
  writer->EndObject();

  const ThemeSurface empty_surface;
  if (appearance.horizontal != empty_surface || appearance.vertical != empty_surface) {
    writer->BeginObjectProperty("surfaces");
    if (appearance.horizontal != empty_surface) {
      WriteSurface(writer, "horizontal", appearance.horizontal);
    }
    if (appearance.vertical != empty_surface) {
      WriteSurface(writer, "vertical", appearance.vertical);
    }
    writer->EndObject();
  }
  writer->EndObject();
}

}  // namespace

bool IsReservedThemeId(std::string_view id) noexcept {
  return id == kDefaultThemeId;
}

ThemeManifest MakeDefaultThemeManifest() {
  ThemeManifest manifest;
  manifest.id = "org.ziliu.default";
  manifest.name = "字流默认";
  manifest.author = "Ziliu Project";
  manifest.version = "1.0.0";
  manifest.license = "GPL-3.0-only";
  manifest.description = "字流内置的简洁候选窗口主题";

  ThemeAppearance dark;
  dark.palette.preedit_text = 0xFFF5F6F7;
  dark.palette.candidate_text = 0xFFF5F6F7;
  dark.palette.highlighted_candidate_text = 0xFF80C8FF;
  dark.palette.background = 0xFF202124;
  dark.palette.highlighted_background = 0xFF123A59;
  dark.palette.muted_text = 0xFFA7AFBA;
  dark.palette.separator = 0xFF4B5159;
  manifest.dark = std::move(dark);
  return manifest;
}

bool IsSafeThemeAssetPath(std::string_view path) {
  if (path.empty() || path.size() > 240 || path.front() == '/' || path.front() == '\\' ||
      path.back() == '/' || path.find('\\') != std::string_view::npos ||
      path.find(':') != std::string_view::npos || !IsValidUtf8(path)) {
    return false;
  }
  std::size_t begin = 0;
  while (begin < path.size()) {
    const std::size_t end = path.find('/', begin);
    const std::string_view component =
        path.substr(begin, end == std::string_view::npos ? path.size() - begin : end - begin);
    if (component.empty() || component == "." || component == ".." ||
        HasControlCharacters(component)) {
      return false;
    }
    if (end == std::string_view::npos) {
      break;
    }
    begin = end + 1;
  }
  return true;
}

std::vector<ThemeManifestIssue> ValidateThemeManifest(const ThemeManifest& manifest) {
  std::vector<ThemeManifestIssue> issues;
  if (manifest.format_version != kThemeManifestVersion) {
    AddValidationIssue(&issues, ThemeManifestIssueCode::kUnsupportedVersion,
                       "$.format_version", "only ZLT manifest version 1 is supported");
  }
  if (!IsValidThemeId(manifest.id)) {
    AddValidationIssue(&issues, ThemeManifestIssueCode::kInvalidProperty, "$.id",
                       "id must use lowercase ASCII letters, digits, '.', '_' or '-'");
  }
  ValidateText(&issues, manifest.name, "$.name", 128, true);
  ValidateText(&issues, manifest.author, "$.author", 128, true);
  ValidateText(&issues, manifest.version, "$.version", 64, true);
  ValidateText(&issues, manifest.license, "$.license", 128, true);
  ValidateText(&issues, manifest.description, "$.description", 2048, false);
  ValidateText(&issues, manifest.homepage, "$.homepage", 2048, false);
  ValidateText(&issues, manifest.source_format, "$.source_format", 64, true);
  ValidateAsset(&issues, manifest.preview_asset, "$.preview", false);
  if (manifest.base_dpi < kMinimumBaseDpi || manifest.base_dpi > kMaximumBaseDpi) {
    AddValidationIssue(&issues, ThemeManifestIssueCode::kInvalidProperty, "$.base_dpi",
                       "base DPI must be between 72 and 384");
  }
  ValidateAppearance(&issues, manifest.light, "$.appearances.light");
  if (manifest.dark.has_value()) {
    ValidateAppearance(&issues, *manifest.dark, "$.appearances.dark");
  }
  return issues;
}

ThemeManifestParseResult ParseThemeManifest(std::string_view json) {
  ThemeManifestParseResult result;
  if (json.size() > kMaximumThemeManifestBytes) {
    result.issues.push_back({ThemeManifestIssueCode::kManifestTooLarge, 0, "$",
                             "manifest exceeds the 256 KiB limit"});
    return result;
  }

  ThemeManifestIssue parse_issue;
  JsonParser parser(json);
  auto root = parser.Parse(&parse_issue);
  if (!root.has_value()) {
    result.issues.push_back(std::move(parse_issue));
    return result;
  }

  ManifestReader reader(*root);
  result = reader.Read();
  if (!result.issues.empty()) {
    return result;
  }
  result.issues = ValidateThemeManifest(result.manifest);
  return result;
}

std::string SerializeThemeManifest(const ThemeManifest& manifest) {
  JsonWriter writer;
  writer.BeginDocument();
  writer.UnsignedProperty("format_version", manifest.format_version);
  writer.StringProperty("id", manifest.id);
  writer.StringProperty("name", manifest.name);
  writer.StringProperty("author", manifest.author);
  writer.StringProperty("version", manifest.version);
  writer.StringProperty("license", manifest.license);
  if (!manifest.description.empty()) {
    writer.StringProperty("description", manifest.description);
  }
  if (!manifest.homepage.empty()) {
    writer.StringProperty("homepage", manifest.homepage);
  }
  if (!manifest.preview_asset.empty()) {
    writer.StringProperty("preview", manifest.preview_asset);
  }
  writer.StringProperty("source_format", manifest.source_format);
  writer.UnsignedProperty("base_dpi", manifest.base_dpi);
  writer.BeginObjectProperty("appearances");
  WriteAppearance(&writer, "light", manifest.light);
  if (manifest.dark.has_value()) {
    WriteAppearance(&writer, "dark", *manifest.dark);
  }
  writer.EndObject();
  return writer.FinishDocument();
}

}  // namespace ziliu::core
