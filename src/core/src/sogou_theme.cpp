#include "ziliu/core/sogou_theme.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <initializer_list>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ziliu::core {
namespace {

constexpr std::size_t kMaximumIniLines = 4096;
constexpr std::size_t kMaximumIniLineBytes = 8192;
constexpr std::size_t kMaximumIniProperties = 4096;
constexpr std::uint32_t kMaximumMappedInset = 4096;

struct IniValue {
  std::string value;
  std::size_t line = 0;
};

using IniSection = std::map<std::string, IniValue>;

struct IniDocument {
  std::map<std::string, IniSection> sections;
};

bool IsAsciiAlpha(char value) {
  return (value >= 'A' && value <= 'Z') ||
         (value >= 'a' && value <= 'z');
}

bool IsAsciiDigit(char value) { return value >= '0' && value <= '9'; }

char AsciiLower(char value) {
  return value >= 'A' && value <= 'Z'
             ? static_cast<char>(value - 'A' + 'a')
             : value;
}

std::string AsciiLowerCopy(std::string_view value) {
  std::string result(value);
  std::transform(result.begin(), result.end(), result.begin(), AsciiLower);
  return result;
}

std::string_view Trim(std::string_view value) {
  while (!value.empty() &&
         (value.front() == ' ' || value.front() == '\t' ||
          value.front() == '\r')) {
    value.remove_prefix(1);
  }
  while (!value.empty() &&
         (value.back() == ' ' || value.back() == '\t' ||
          value.back() == '\r')) {
    value.remove_suffix(1);
  }
  return value;
}

bool IsValidUtf8(std::string_view text) {
  std::size_t index = 0;
  while (index < text.size()) {
    const auto first = static_cast<unsigned char>(text[index]);
    if (first <= 0x7FU) {
      ++index;
      continue;
    }
    std::size_t continuation_count = 0;
    std::uint32_t code_point = 0;
    std::uint32_t minimum = 0;
    if (first >= 0xC2U && first <= 0xDFU) {
      continuation_count = 1;
      code_point = first & 0x1FU;
      minimum = 0x80U;
    } else if (first >= 0xE0U && first <= 0xEFU) {
      continuation_count = 2;
      code_point = first & 0x0FU;
      minimum = 0x800U;
    } else if (first >= 0xF0U && first <= 0xF4U) {
      continuation_count = 3;
      code_point = first & 0x07U;
      minimum = 0x10000U;
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
    if (code_point < minimum || code_point > 0x10FFFFU ||
        (code_point >= 0xD800U && code_point <= 0xDFFFU)) {
      return false;
    }
    index += continuation_count + 1;
  }
  return true;
}

bool AppendUtf8(std::uint32_t code_point, std::string* output) {
  std::array<char, 4> encoded{};
  std::size_t count = 0;
  if (code_point <= 0x7FU) {
    encoded[0] = static_cast<char>(code_point);
    count = 1U;
  } else if (code_point <= 0x7FFU) {
    encoded[0] = static_cast<char>(0xC0U | (code_point >> 6U));
    encoded[1] = static_cast<char>(0x80U | (code_point & 0x3FU));
    count = 2U;
  } else if (code_point <= 0xFFFFU) {
    encoded[0] = static_cast<char>(0xE0U | (code_point >> 12U));
    encoded[1] =
        static_cast<char>(0x80U | ((code_point >> 6U) & 0x3FU));
    encoded[2] = static_cast<char>(0x80U | (code_point & 0x3FU));
    count = 3U;
  } else if (code_point <= 0x10FFFFU) {
    encoded[0] = static_cast<char>(0xF0U | (code_point >> 18U));
    encoded[1] =
        static_cast<char>(0x80U | ((code_point >> 12U) & 0x3FU));
    encoded[2] =
        static_cast<char>(0x80U | ((code_point >> 6U) & 0x3FU));
    encoded[3] = static_cast<char>(0x80U | (code_point & 0x3FU));
    count = 4U;
  } else {
    return false;
  }
  if (output->size() > kMaximumSogouThemeIniBytes ||
      count > kMaximumSogouThemeIniBytes - output->size()) {
    return false;
  }
  output->append(encoded.data(), count);
  return true;
}

bool IsRootSkinIni(std::string_view path) {
  return path.find_first_of("/\\") == std::string_view::npos &&
         AsciiLowerCopy(path) == "skin.ini";
}

bool HasUnsupportedControl(std::string_view text) {
  return std::any_of(text.begin(), text.end(), [](char value) {
    const auto byte = static_cast<unsigned char>(value);
    return byte < 0x20U && value != '\r' && value != '\n' && value != '\t';
  });
}

bool IsIniIdentifier(std::string_view value) {
  return !value.empty() &&
         std::all_of(value.begin(), value.end(), [](char character) {
           return IsAsciiAlpha(character) || IsAsciiDigit(character) ||
                  character == '_';
         });
}

bool IsLowerSha256(std::string_view value) {
  return value.size() == 64U &&
         std::all_of(value.begin(), value.end(), [](char byte) {
           return (byte >= '0' && byte <= '9') ||
                  (byte >= 'a' && byte <= 'f');
         });
}

void AddIssue(SogouThemeConversion* conversion, SogouThemeIssueCode code,
              std::size_t line, std::string path, std::string message) {
  conversion->issues.push_back(
      {code, line, std::move(path), std::move(message)});
}

std::optional<IniDocument> ParseIni(std::string_view input,
                                    SogouThemeConversion* conversion) {
  if (input.size() > kMaximumSogouThemeIniBytes) {
    AddIssue(conversion, SogouThemeIssueCode::kIniTooLarge, 0, "$",
             "skin.ini exceeds the 256 KiB limit");
    return std::nullopt;
  }
  if (!IsValidUtf8(input)) {
    AddIssue(conversion, SogouThemeIssueCode::kInvalidUtf8, 0, "$",
             "skin.ini must be valid UTF-8");
    return std::nullopt;
  }
  if (HasUnsupportedControl(input)) {
    AddIssue(conversion, SogouThemeIssueCode::kMalformedIni, 0, "$",
             "skin.ini contains an unsupported control character");
    return std::nullopt;
  }
  if (input.starts_with("\xEF\xBB\xBF")) {
    input.remove_prefix(3);
  }

  IniDocument document;
  std::set<std::string> declared_sections;
  std::string current_section;
  std::size_t line_number = 0;
  std::size_t property_count = 0;
  std::size_t begin = 0;
  while (begin <= input.size()) {
    ++line_number;
    if (line_number > kMaximumIniLines) {
      AddIssue(conversion, SogouThemeIssueCode::kMalformedIni, line_number,
               "$", "skin.ini contains too many lines");
      return std::nullopt;
    }
    const std::size_t end = input.find('\n', begin);
    const std::string_view raw_line = input.substr(
        begin, end == std::string_view::npos ? input.size() - begin
                                             : end - begin);
    if (raw_line.size() > kMaximumIniLineBytes) {
      AddIssue(conversion, SogouThemeIssueCode::kMalformedIni, line_number,
               "$", "skin.ini line exceeds 8192 bytes");
      return std::nullopt;
    }
    const std::string_view line = Trim(raw_line);
    if (!line.empty() && line.front() != ';' && line.front() != '#') {
      if (line.front() == '[') {
        if (line.size() < 3U || line.back() != ']') {
          AddIssue(conversion, SogouThemeIssueCode::kMalformedIni,
                   line_number, "$", "section header is malformed");
          return std::nullopt;
        }
        const std::string_view section_name =
            Trim(line.substr(1, line.size() - 2U));
        if (!IsIniIdentifier(section_name)) {
          AddIssue(conversion, SogouThemeIssueCode::kMalformedIni,
                   line_number, "$", "section name is invalid");
          return std::nullopt;
        }
        current_section = AsciiLowerCopy(section_name);
        if (!declared_sections.insert(current_section).second) {
          AddIssue(conversion, SogouThemeIssueCode::kDuplicateProperty,
                   line_number, current_section,
                   "duplicate section is not allowed");
          return std::nullopt;
        }
        document.sections.try_emplace(current_section);
      } else {
        if (current_section.empty()) {
          AddIssue(conversion, SogouThemeIssueCode::kMalformedIni,
                   line_number, "$", "property precedes the first section");
          return std::nullopt;
        }
        const std::size_t equals = line.find('=');
        if (equals == std::string_view::npos) {
          AddIssue(conversion, SogouThemeIssueCode::kMalformedIni,
                   line_number, current_section, "property lacks '='");
          return std::nullopt;
        }
        const std::string_view raw_key = Trim(line.substr(0, equals));
        if (!IsIniIdentifier(raw_key)) {
          AddIssue(conversion, SogouThemeIssueCode::kMalformedIni,
                   line_number, current_section, "property name is invalid");
          return std::nullopt;
        }
        ++property_count;
        if (property_count > kMaximumIniProperties) {
          AddIssue(conversion, SogouThemeIssueCode::kMalformedIni,
                   line_number, "$", "skin.ini has too many properties");
          return std::nullopt;
        }
        const std::string key = AsciiLowerCopy(raw_key);
        IniSection& section = document.sections[current_section];
        if (section.contains(key)) {
          AddIssue(conversion, SogouThemeIssueCode::kDuplicateProperty,
                   line_number, current_section + "." + key,
                   "duplicate property is not allowed");
          return std::nullopt;
        }
        section.emplace(key,
                        IniValue{std::string(Trim(line.substr(equals + 1U))),
                                 line_number});
      }
    }
    if (end == std::string_view::npos) {
      break;
    }
    begin = end + 1U;
  }
  return document;
}

const IniSection* FindSection(const IniDocument& document,
                              std::string_view section_name) {
  const auto iterator = document.sections.find(AsciiLowerCopy(section_name));
  return iterator == document.sections.end() ? nullptr : &iterator->second;
}

const IniValue* FindValue(const IniDocument& document,
                          std::string_view section_name,
                          std::string_view key) {
  const IniSection* section = FindSection(document, section_name);
  if (section == nullptr) {
    return nullptr;
  }
  const auto iterator = section->find(AsciiLowerCopy(key));
  return iterator == section->end() ? nullptr : &iterator->second;
}

const IniValue* FindFirstValue(
    const IniDocument& document, std::string_view section_name,
    std::initializer_list<std::string_view> keys) {
  for (const std::string_view key : keys) {
    if (const IniValue* value = FindValue(document, section_name, key);
        value != nullptr && !value->value.empty()) {
      return value;
    }
  }
  return nullptr;
}

std::string DisplayNameFromSourceHint(std::string_view source_hint) {
  const std::size_t slash = source_hint.find_last_of("/\\");
  if (slash != std::string_view::npos) {
    source_hint.remove_prefix(slash + 1U);
  }
  if (source_hint.size() >= 4U &&
      AsciiLowerCopy(source_hint.substr(source_hint.size() - 4U)) == ".ssf") {
    source_hint.remove_suffix(4U);
  }
  source_hint = Trim(source_hint);
  return source_hint.empty() ? "Imported Sogou SSF"
                             : std::string(source_hint);
}

std::string NormalizeIdComponent(std::string_view value,
                                 std::size_t maximum_length) {
  std::string result;
  result.reserve(std::min(value.size(), maximum_length));
  bool pending_separator = false;
  for (const char character : value) {
    if (IsAsciiAlpha(character) || IsAsciiDigit(character)) {
      if (pending_separator && !result.empty() && result.back() != '-' &&
          result.size() < maximum_length) {
        result.push_back('-');
      }
      pending_separator = false;
      if (result.size() < maximum_length) {
        result.push_back(AsciiLower(character));
      }
    } else if (character == '.' || character == '_' || character == '-') {
      if (!result.empty() && result.size() < maximum_length) {
        result.push_back(character);
      }
      pending_separator = false;
    } else {
      pending_separator = true;
    }
  }
  while (!result.empty() &&
         (result.back() == '.' || result.back() == '_' ||
          result.back() == '-')) {
    result.pop_back();
  }
  return result;
}

std::string MakeThemeId(const IniDocument& document,
                        std::string_view display_name,
                        std::string_view package_sha256) {
  if (const IniValue* skin_id = FindValue(document, "General", "skin_id");
      skin_id != nullptr && !skin_id->value.empty()) {
    std::string normalized = NormalizeIdComponent(skin_id->value, 120U);
    if (!normalized.empty()) {
      return "sogou." + normalized;
    }
  }
  std::string slug = NormalizeIdComponent(display_name, 88U);
  if (slug.empty()) {
    slug = "skin";
  }
  const std::string_view suffix =
      package_sha256.size() >= 16U ? package_sha256.substr(0, 16U)
                                   : std::string_view("invalid");
  return "sogou." + slug + "-" + std::string(suffix);
}

bool ParseUnsigned(std::string_view text, std::uint32_t maximum,
                   std::uint32_t* output) {
  text = Trim(text);
  if (text.empty() || text.front() == '-' || text.front() == '+') {
    return false;
  }
  int base = 10;
  if (text.size() > 2U && text[0] == '0' &&
      (text[1] == 'x' || text[1] == 'X')) {
    text.remove_prefix(2U);
    base = 16;
  }
  if (text.empty()) {
    return false;
  }
  std::uint64_t parsed = 0;
  const auto result =
      std::from_chars(text.data(), text.data() + text.size(), parsed, base);
  if (result.ec != std::errc{} || result.ptr != text.data() + text.size() ||
      parsed > maximum) {
    return false;
  }
  *output = static_cast<std::uint32_t>(parsed);
  return true;
}

std::vector<std::string_view> SplitComma(std::string_view value) {
  std::vector<std::string_view> fields;
  std::size_t begin = 0;
  while (begin <= value.size()) {
    const std::size_t end = value.find(',', begin);
    fields.push_back(Trim(value.substr(
        begin, end == std::string_view::npos ? value.size() - begin
                                             : end - begin)));
    if (end == std::string_view::npos) {
      break;
    }
    begin = end + 1U;
  }
  return fields;
}

std::optional<std::vector<std::uint32_t>> ParseNumberList(
    const IniValue& value, std::size_t expected_count, std::uint32_t maximum,
    std::string path, SogouThemeConversion* conversion) {
  const std::vector<std::string_view> fields = SplitComma(value.value);
  if (fields.size() != expected_count) {
    AddIssue(conversion, SogouThemeIssueCode::kInvalidValue, value.line,
             std::move(path), "property has the wrong number of values");
    return std::nullopt;
  }
  std::vector<std::uint32_t> numbers;
  numbers.reserve(fields.size());
  for (const std::string_view field : fields) {
    std::uint32_t number = 0;
    if (!ParseUnsigned(field, maximum, &number)) {
      AddIssue(conversion, SogouThemeIssueCode::kInvalidValue, value.line,
               std::move(path), "property contains an invalid integer");
      return std::nullopt;
    }
    numbers.push_back(number);
  }
  return numbers;
}

std::optional<ThemePoint> ParsePoint(const IniValue& value, std::string path,
                                     SogouThemeConversion* conversion) {
  const std::vector<std::string_view> fields = SplitComma(value.value);
  if (fields.size() != 2U) {
    AddIssue(conversion, SogouThemeIssueCode::kInvalidValue, value.line,
             std::move(path), "anchor must contain two integers");
    return std::nullopt;
  }
  std::int32_t coordinates[2]{};
  for (std::size_t index = 0; index < 2U; ++index) {
    const auto parsed = std::from_chars(
        fields[index].data(), fields[index].data() + fields[index].size(),
        coordinates[index]);
    if (fields[index].empty() || parsed.ec != std::errc{} ||
        parsed.ptr != fields[index].data() + fields[index].size() ||
        coordinates[index] < -4096 || coordinates[index] > 4096) {
      AddIssue(conversion, SogouThemeIssueCode::kInvalidValue, value.line,
               std::move(path), "anchor coordinates are out of range");
      return std::nullopt;
    }
  }
  return ThemePoint{coordinates[0], coordinates[1]};
}

std::optional<std::array<std::int32_t, 10>> ParseRawAlignment(
    const IniValue& value, std::string path,
    SogouThemeConversion* conversion) {
  const std::vector<std::string_view> fields = SplitComma(value.value);
  if (fields.size() != 10U) {
    AddIssue(conversion, SogouThemeIssueCode::kInvalidValue, value.line,
             std::move(path), "custom alignment must contain ten integers");
    return std::nullopt;
  }
  std::array<std::int32_t, 10> result{};
  for (std::size_t index = 0; index < result.size(); ++index) {
    const auto parsed = std::from_chars(fields[index].data(),
                                        fields[index].data() + fields[index].size(),
                                        result[index]);
    if (fields[index].empty() || parsed.ec != std::errc{} ||
        parsed.ptr != fields[index].data() + fields[index].size()) {
      AddIssue(conversion, SogouThemeIssueCode::kInvalidValue, value.line,
               std::move(path), "custom alignment contains an invalid integer");
      return std::nullopt;
    }
  }
  return result;
}

std::optional<ThemeImageLayout> MapLayoutMode(
    std::uint32_t mode, bool horizontal, const IniValue& value,
    std::string path, SogouThemeConversion* conversion) {
  if (mode == 0U) {
    return ThemeImageLayout::kStretch;
  }
  if (mode == 1U) {
    return ThemeImageLayout::kTile;
  }
  if (!horizontal && mode == 2U) {
    return ThemeImageLayout::kFixed;
  }
  AddIssue(conversion, SogouThemeIssueCode::kInvalidValue, value.line,
           std::move(path), "layout mode is not supported for this axis");
  return std::nullopt;
}

std::optional<std::uint32_t> ParseBgrColor(
    const IniValue& value, std::string path,
    SogouThemeConversion* conversion) {
  std::uint32_t bgr = 0;
  if (!ParseUnsigned(value.value, 0xFFFFFFU, &bgr)) {
    AddIssue(conversion, SogouThemeIssueCode::kInvalidValue, value.line,
             std::move(path), "color must be a 24-bit BGR integer");
    return std::nullopt;
  }
  const std::uint32_t red = bgr & 0xFFU;
  const std::uint32_t green = (bgr >> 8U) & 0xFFU;
  const std::uint32_t blue = (bgr >> 16U) & 0xFFU;
  return 0xFF000000U | (red << 16U) | (green << 8U) | blue;
}

bool IsSupportedSourceImage(std::string_view path) {
  const std::size_t dot = path.find_last_of('.');
  if (dot == std::string_view::npos) {
    return false;
  }
  const std::string extension = AsciiLowerCopy(path.substr(dot));
  return extension == ".png" || extension == ".apng" ||
         extension == ".bmp" || extension == ".jpg" ||
         extension == ".jpeg" || extension == ".gif";
}

bool IsWindowsReservedComponent(std::string_view component) {
  const std::size_t dot = component.find('.');
  const std::string base = AsciiLowerCopy(component.substr(0, dot));
  if (base == "con" || base == "prn" || base == "aux" || base == "nul") {
    return true;
  }
  return base.size() == 4U &&
         (base.starts_with("com") || base.starts_with("lpt")) &&
         base[3] >= '1' && base[3] <= '9';
}

std::optional<std::string> NormalizeSourceAssetPath(std::string_view path) {
  path = Trim(path);
  if (path.empty() || path.size() > 240U || path.front() == '/' ||
      path.front() == '\\' || path.back() == '/' || path.back() == '\\' ||
      path.find(':') != std::string_view::npos) {
    return std::nullopt;
  }
  std::string normalized(path);
  std::replace(normalized.begin(), normalized.end(), '\\', '/');
  std::size_t begin = 0;
  while (begin < normalized.size()) {
    const std::size_t end = normalized.find('/', begin);
    const std::string_view component(
        normalized.data() + begin,
        (end == std::string::npos ? normalized.size() : end) - begin);
    if (component.empty() || component == "." || component == ".." ||
        component.back() == ' ' || component.back() == '.' ||
        IsWindowsReservedComponent(component) ||
        HasUnsupportedControl(component)) {
      return std::nullopt;
    }
    if (end == std::string::npos) {
      break;
    }
    begin = end + 1U;
  }
  if (!IsSupportedSourceImage(normalized)) {
    return std::nullopt;
  }
  return normalized;
}

class AssetCollector {
 public:
  explicit AssetCollector(SogouThemeConversion* conversion)
      : conversion_(conversion) {}

  std::optional<std::string> Register(const IniValue& source,
                                      std::string path) {
    const auto normalized = NormalizeSourceAssetPath(source.value);
    if (!normalized.has_value()) {
      AddIssue(conversion_, SogouThemeIssueCode::kUnsafeAssetPath,
               source.line, std::move(path),
               "asset must be a safe relative supported image path");
      return std::nullopt;
    }
    const std::string key = AsciiLowerCopy(*normalized);
    if (const auto existing = targets_by_source_.find(key);
        existing != targets_by_source_.end()) {
      return existing->second;
    }
    std::string index = std::to_string(conversion_->assets.size());
    if (index.size() < 3U) {
      index.insert(index.begin(), 3U - index.size(), '0');
    }
    const std::string target = "assets/ssf-" + index + ".png";
    conversion_->assets.push_back({*normalized, target});
    targets_by_source_.emplace(key, target);
    return target;
  }

 private:
  SogouThemeConversion* conversion_;
  std::map<std::string, std::string> targets_by_source_;
};

void MapMetadata(const IniDocument& document, std::string_view source_hint,
                 std::string_view package_sha256, AssetCollector* assets,
                 SogouThemeConversion* conversion) {
  ThemeManifest& manifest = conversion->manifest;
  const IniValue* name =
      FindFirstValue(document, "General", {"skin_name", "name"});
  manifest.name = name == nullptr ? DisplayNameFromSourceHint(source_hint)
                                  : name->value;
  manifest.id = MakeThemeId(document, manifest.name, package_sha256);
  if (const IniValue* value =
          FindFirstValue(document, "General", {"skin_author", "author"});
      value != nullptr) {
    manifest.author = value->value;
  }
  if (const IniValue* value =
          FindFirstValue(document, "General", {"skin_version", "version"});
      value != nullptr) {
    manifest.version = value->value;
  }
  if (const IniValue* value =
          FindFirstValue(document, "General", {"skin_info", "info"});
      value != nullptr) {
    manifest.description = value->value;
  }
  manifest.source_format = "sogou-ssf";
  manifest.source_package_sha256 = std::string(package_sha256);
  manifest.base_dpi = 96U;
  if (!IsLowerSha256(package_sha256)) {
    AddIssue(conversion, SogouThemeIssueCode::kInvalidValue, 0,
             "$.source_package_sha256",
             "source package identity must be a lowercase SHA-256 digest");
  }
  if (const IniValue* preview = FindFirstValue(
          document, "General", {"preview_square", "preview_comp"});
      preview != nullptr) {
    if (const auto target = assets->Register(*preview, "General.preview");
        target.has_value()) {
      manifest.preview_asset = *target;
    }
  }
}

void MapDisplay(const IniDocument& document,
                SogouThemeConversion* conversion) {
  ThemeAppearance& appearance = conversion->manifest.appearance;
  if (const IniValue* value = FindValue(document, "Display", "font_ch");
      value != nullptr && !value->value.empty()) {
    appearance.typography.chinese_font_family = value->value;
  }
  if (const IniValue* value = FindValue(document, "Display", "font_en");
      value != nullptr && !value->value.empty()) {
    appearance.typography.english_font_family = value->value;
  }
  if (const IniValue* value = FindValue(document, "Display", "font_size");
      value != nullptr) {
    std::uint32_t parsed = 0;
    if (!ParseUnsigned(value->value, 96U, &parsed) || parsed < 8U) {
      AddIssue(conversion, SogouThemeIssueCode::kInvalidValue, value->line,
               "Display.font_size", "font_size must be between 8 and 96");
    } else {
      appearance.typography.font_size = parsed;
    }
  }
  if (const IniValue* value = FindValue(document, "Display", "use_gdip");
      value != nullptr) {
    std::uint32_t parsed = 0;
    if (!ParseUnsigned(value->value, 1U, &parsed)) {
      AddIssue(conversion, SogouThemeIssueCode::kInvalidValue, value->line,
               "Display.use_gdip", "use_gdip must be 0 or 1");
    } else {
      appearance.typography.text_renderer =
          parsed == 0U ? ThemeTextRenderer::kSogouGdi
                       : ThemeTextRenderer::kSogouGdiPlus;
    }
  }

  const auto map_color = [&](std::string_view key,
                             std::optional<std::uint32_t>* destination) {
    if (const IniValue* value = FindValue(document, "Display", key);
        value != nullptr) {
      if (const auto color =
              ParseBgrColor(*value, "Display." + std::string(key), conversion);
          color.has_value()) {
        *destination = *color;
      }
    }
  };
  map_color("pinyin_color", &appearance.palette.preedit_text);
  map_color("zhongwen_color", &appearance.palette.candidate_text);
  map_color("zhongwen_first_color",
            &appearance.palette.highlighted_candidate_text);
  map_color("comphint_color", &appearance.palette.annotation_text);
  map_color("caret_color", &appearance.palette.caret_text);
  map_color("zhongwen_first_bk_color",
            &appearance.palette.highlighted_background);
}

std::optional<ThemeButtonImages> MapButton(
    const IniDocument& document, std::string_view section_name,
    std::string_view prefix, AssetCollector* assets,
    SogouThemeConversion* conversion) {
  const std::string display_key = std::string(prefix) + "_display";
  bool explicitly_enabled = false;
  if (const IniValue* display =
          FindValue(document, section_name, display_key);
      display != nullptr) {
    std::uint32_t enabled = 0;
    if (!ParseUnsigned(display->value, 1U, &enabled)) {
      AddIssue(conversion, SogouThemeIssueCode::kInvalidValue, display->line,
               std::string(section_name) + "." + display_key,
               "button display flag must be 0 or 1");
      return std::nullopt;
    }
    if (enabled == 0U) {
      return std::nullopt;
    }
    explicitly_enabled = true;
  }
  const IniValue* normal = FindValue(document, section_name, prefix);
  if (normal == nullptr) {
    if (explicitly_enabled) {
      AddIssue(conversion, SogouThemeIssueCode::kMissingProperty, 0,
               std::string(section_name) + "." + std::string(prefix),
               "enabled button must provide an image");
    }
    return std::nullopt;
  }
  const std::vector<std::string_view> normal_fields = SplitComma(normal->value);
  const auto first_normal = std::find_if(
      normal_fields.begin(), normal_fields.end(),
      [](std::string_view field) { return !field.empty(); });
  if (first_normal == normal_fields.end()) {
    AddIssue(conversion, SogouThemeIssueCode::kInvalidValue, normal->line,
             std::string(section_name) + "." + std::string(prefix),
             "button must provide at least one image");
    return std::nullopt;
  }
  ThemeButtonImages button;
  const IniValue normal_asset{std::string(*first_normal), normal->line};
  if (const auto target = assets->Register(
          normal_asset, std::string(section_name) + "." + std::string(prefix));
      target.has_value()) {
    button.normal = *target;
  }
  const auto map_state = [&](std::string_view suffix,
                             std::string* destination) {
    const std::string key = std::string(prefix) + std::string(suffix);
    const IniValue* state = FindValue(document, section_name, key);
    if (state == nullptr) {
      return;
    }
    const std::vector<std::string_view> fields = SplitComma(state->value);
    const auto first = std::find_if(fields.begin(), fields.end(),
                                    [](std::string_view field) {
                                      return !field.empty();
                                    });
    if (first == fields.end()) {
      return;
    }
    const IniValue state_asset{std::string(*first), state->line};
    if (const auto target = assets->Register(
            state_asset, std::string(section_name) + "." + key);
        target.has_value()) {
      *destination = *target;
    }
  };
  map_state("_hover", &button.hover);
  map_state("_down", &button.pressed);
  return button.normal.empty() ? std::nullopt
                               : std::optional<ThemeButtonImages>(button);
}

void MapOverlay(const IniDocument& document, std::string_view section_name,
                std::uint32_t custom_index, ThemeSurface* surface,
                AssetCollector* assets, SogouThemeConversion* conversion) {
  const std::string prefix = "custom" + std::to_string(custom_index);
  const std::string display_key = prefix + "_display";
  const IniValue* display = FindValue(document, section_name, display_key);
  if (display == nullptr) {
    return;
  }
  std::uint32_t enabled = 0;
  if (!ParseUnsigned(display->value, 1U, &enabled)) {
    AddIssue(conversion, SogouThemeIssueCode::kInvalidValue, display->line,
             std::string(section_name) + "." + display_key,
             "custom display flag must be 0 or 1");
    return;
  }
  if (enabled == 0U) {
    return;
  }
  const IniValue* image = FindValue(document, section_name, prefix);
  const std::string align_key = prefix + "_align";
  const IniValue* alignment = FindValue(document, section_name, align_key);
  if (image == nullptr || image->value.empty()) {
    AddIssue(conversion, SogouThemeIssueCode::kMissingProperty,
             image == nullptr ? 0U : image->line,
             std::string(section_name) + "." + prefix,
             "enabled custom overlay must provide an image");
  }
  if (alignment == nullptr) {
    AddIssue(conversion, SogouThemeIssueCode::kMissingProperty, 0,
             std::string(section_name) + "." + align_key,
             "enabled custom overlay must provide ten integers");
  }
  std::optional<std::string> target;
  if (image != nullptr && !image->value.empty()) {
    target = assets->Register(
        *image, std::string(section_name) + "." + prefix);
  }
  std::optional<std::array<std::int32_t, 10>> raw_alignment;
  if (alignment != nullptr) {
    raw_alignment = ParseRawAlignment(
        *alignment, std::string(section_name) + "." + align_key, conversion);
  }
  if (target.has_value() && raw_alignment.has_value()) {
    surface->overlays.push_back(
        {*target, custom_index, custom_index, *raw_alignment});
  }
}

void MapOverlays(const IniDocument& document, std::string_view section_name,
                 ThemeSurface* surface, AssetCollector* assets,
                 SogouThemeConversion* conversion) {
  const IniValue* count = FindValue(document, section_name, "custom_cnt");
  if (count == nullptr) {
    return;
  }
  std::uint32_t parsed = 0;
  if (!ParseUnsigned(count->value,
                     static_cast<std::uint32_t>(
                         kMaximumThemeSurfaceOverlays),
                     &parsed)) {
    AddIssue(conversion, SogouThemeIssueCode::kInvalidValue, count->line,
             std::string(section_name) + ".custom_cnt",
             "custom_cnt must be between 0 and 128");
    return;
  }
  for (std::uint32_t index = 0; index < parsed; ++index) {
    MapOverlay(document, section_name, index, surface, assets, conversion);
  }
  if (parsed != 0U) {
    MapOverlay(document, section_name, parsed, surface, assets, conversion);
  }
}

void MapSurface(const IniDocument& document, std::string_view section_name,
                std::optional<ThemeSurface>* destination,
                AssetCollector* assets, SogouThemeConversion* conversion) {
  if (FindSection(document, section_name) == nullptr) {
    return;
  }
  destination->emplace();
  ThemeSurface& surface = **destination;
  if (const IniValue* anchor = FindValue(document, section_name, "anchor");
      anchor != nullptr) {
    surface.anchor = ParsePoint(*anchor,
                                std::string(section_name) + ".anchor",
                                conversion);
  }

  ThemeImage background;
  bool has_background = false;
  if (const IniValue* picture = FindValue(document, section_name, "pic");
      picture != nullptr && !picture->value.empty()) {
    if (const auto target = assets->Register(
            *picture, std::string(section_name) + ".pic");
        target.has_value()) {
      background.asset = *target;
      has_background = true;
    }
  }
  if (const IniValue* layout =
          FindValue(document, section_name, "layout_horizontal");
      layout != nullptr) {
    if (const auto values = ParseNumberList(
            *layout, 3U, kMaximumMappedInset,
            std::string(section_name) + ".layout_horizontal", conversion);
        values.has_value()) {
      background.horizontal_layout = MapLayoutMode(
          (*values)[0], true, *layout,
          std::string(section_name) + ".layout_horizontal", conversion);
      background.stretch.left = (*values)[1];
      background.stretch.right = (*values)[2];
    }
  }
  if (const IniValue* layout =
          FindValue(document, section_name, "layout_vertical");
      layout != nullptr) {
    if (const auto values = ParseNumberList(
            *layout, 3U, kMaximumMappedInset,
            std::string(section_name) + ".layout_vertical", conversion);
        values.has_value()) {
      background.vertical_layout = MapLayoutMode(
          (*values)[0], false, *layout,
          std::string(section_name) + ".layout_vertical", conversion);
      background.stretch.top = (*values)[1];
      background.stretch.bottom = (*values)[2];
    }
  }
  if (has_background) {
    surface.background = std::move(background);
  }

  MapOverlays(document, section_name, &surface, assets, conversion);
  const auto map_insets = [&](std::string_view key,
                              std::optional<ThemeInsets>* target) {
    const IniValue* value = FindValue(document, section_name, key);
    if (value == nullptr) {
      return;
    }
    if (const auto numbers = ParseNumberList(
            *value, 4U, kMaximumMappedInset,
            std::string(section_name) + "." + std::string(key), conversion);
        numbers.has_value()) {
      *target = ThemeInsets{(*numbers)[2], (*numbers)[0], (*numbers)[3],
                            (*numbers)[1]};
    }
  };
  map_insets("pinyin_marge", &surface.preedit_insets);
  map_insets("zhongwen_marge", &surface.candidate_insets);

  if (const IniValue* separator =
          FindValue(document, section_name, "separator");
      separator != nullptr) {
    const std::vector<std::string_view> fields = SplitComma(separator->value);
    if (fields.size() != 3U) {
      AddIssue(conversion, SogouThemeIssueCode::kInvalidValue,
               separator->line, std::string(section_name) + ".separator",
               "separator must contain color,left,right");
    } else {
      const IniValue color_value{std::string(fields[0]), separator->line};
      std::uint32_t left = 0;
      std::uint32_t right = 0;
      const auto color = ParseBgrColor(
          color_value, std::string(section_name) + ".separator.color",
          conversion);
      const bool valid_left =
          ParseUnsigned(fields[1], kMaximumMappedInset, &left);
      const bool valid_right =
          ParseUnsigned(fields[2], kMaximumMappedInset, &right);
      if (!valid_left || !valid_right) {
        AddIssue(conversion, SogouThemeIssueCode::kInvalidValue,
                 separator->line, std::string(section_name) + ".separator",
                 "separator margins are out of range");
      } else if (color.has_value()) {
        surface.separator = ThemeSeparator{*color, left, right, 1U};
      }
    }
  }
  surface.previous_button =
      MapButton(document, section_name, "pageup", assets, conversion);
  surface.next_button =
      MapButton(document, section_name, "pagedown", assets, conversion);
}

void AppendManifestIssues(SogouThemeConversion* conversion) {
  for (const ThemeManifestIssue& issue :
       ValidateThemeManifest(conversion->manifest)) {
    AddIssue(conversion, SogouThemeIssueCode::kInvalidManifest, 0, issue.path,
             issue.message);
  }
}

}  // namespace

SogouThemeTextNormalization NormalizeSogouThemeIniText(
    std::span<const std::uint8_t> bytes) {
  SogouThemeTextNormalization result;
  const auto has_prefix = [&](std::initializer_list<std::uint8_t> prefix) {
    return bytes.size() >= prefix.size() &&
           std::equal(prefix.begin(), prefix.end(), bytes.begin());
  };
  if (has_prefix({0x00U, 0x00U, 0xFEU, 0xFFU}) ||
      has_prefix({0xFFU, 0xFEU, 0x00U, 0x00U})) {
    result.error = "skin.ini UTF-32 encoding is unsupported";
    return result;
  }
  if (has_prefix({0xFEU, 0xFFU})) {
    result.error = "skin.ini UTF-16BE encoding is unsupported";
    return result;
  }

  if (has_prefix({0xFFU, 0xFEU})) {
    result.encoding = SogouThemeIniEncoding::kUtf16LeBom;
    const auto payload = bytes.subspan(2U);
    if (payload.size() % 2U != 0U) {
      result.error = "skin.ini UTF-16LE payload has an odd byte length";
      return result;
    }
    result.utf8.reserve((std::min)(payload.size(),
                                   kMaximumSogouThemeIniBytes));
    for (std::size_t offset = 0; offset < payload.size(); offset += 2U) {
      const std::uint16_t first =
          static_cast<std::uint16_t>(payload[offset]) |
          static_cast<std::uint16_t>(
              static_cast<std::uint16_t>(payload[offset + 1U]) << 8U);
      if (first == 0U) {
        result.error = "skin.ini contains an embedded NUL";
        result.utf8.clear();
        return result;
      }
      std::uint32_t code_point = first;
      if (first >= 0xD800U && first <= 0xDBFFU) {
        if (offset + 3U >= payload.size()) {
          result.error = "skin.ini contains an unpaired UTF-16 high surrogate";
          result.utf8.clear();
          return result;
        }
        const std::uint16_t second =
            static_cast<std::uint16_t>(payload[offset + 2U]) |
            static_cast<std::uint16_t>(
                static_cast<std::uint16_t>(payload[offset + 3U]) << 8U);
        if (second < 0xDC00U || second > 0xDFFFU) {
          result.error = "skin.ini contains an unpaired UTF-16 high surrogate";
          result.utf8.clear();
          return result;
        }
        code_point = 0x10000U +
                     ((static_cast<std::uint32_t>(first) - 0xD800U) << 10U) +
                     (static_cast<std::uint32_t>(second) - 0xDC00U);
        offset += 2U;
      } else if (first >= 0xDC00U && first <= 0xDFFFU) {
        result.error = "skin.ini contains an unpaired UTF-16 low surrogate";
        result.utf8.clear();
        return result;
      }
      if (!AppendUtf8(code_point, &result.utf8)) {
        result.error = "normalized skin.ini exceeds the 256 KiB limit";
        result.utf8.clear();
        return result;
      }
    }
    return result;
  }

  std::size_t offset = 0;
  if (has_prefix({0xEFU, 0xBBU, 0xBFU})) {
    result.encoding = SogouThemeIniEncoding::kUtf8Bom;
    offset = 3U;
  } else {
    result.encoding = SogouThemeIniEncoding::kUtf8;
  }
  const auto payload = bytes.subspan(offset);
  if (payload.size() > kMaximumSogouThemeIniBytes) {
    result.error = "skin.ini exceeds the 256 KiB limit";
    return result;
  }
  if (!payload.empty()) {
    result.utf8.assign(reinterpret_cast<const char*>(payload.data()),
                       payload.size());
  }
  if (!IsValidUtf8(result.utf8)) {
    result.error = "skin.ini is not valid UTF-8";
    result.utf8.clear();
    return result;
  }
  if (result.utf8.find('\0') != std::string::npos) {
    result.error = "skin.ini contains an embedded NUL";
    result.utf8.clear();
    return result;
  }
  return result;
}

SogouThemeConversion ConvertSogouThemeIni(
    std::string_view utf8_ini, std::string_view source_hint,
    std::string_view source_package_sha256) {
  SogouThemeConversion conversion;
  const auto document = ParseIni(utf8_ini, &conversion);
  if (!document.has_value()) {
    return conversion;
  }
  AssetCollector assets(&conversion);
  MapMetadata(*document, source_hint, source_package_sha256, &assets,
              &conversion);
  MapDisplay(*document, &conversion);
  const bool has_horizontal = FindSection(*document, "Scheme_H1") != nullptr;
  const bool has_vertical = FindSection(*document, "Scheme_V1") != nullptr;
  if (!has_horizontal && !has_vertical) {
    AddIssue(&conversion, SogouThemeIssueCode::kMissingProperty, 0, "$",
             "skin.ini must contain Scheme_H1 or Scheme_V1");
  }
  MapSurface(*document, "Scheme_H1", &conversion.manifest.appearance.horizontal,
             &assets, &conversion);
  MapSurface(*document, "Scheme_V1", &conversion.manifest.appearance.vertical,
             &assets, &conversion);
  AppendManifestIssues(&conversion);
  return conversion;
}

SogouThemePackageConversion ConvertSogouThemePackage(
    std::span<const SogouThemePackageEntryView> entries,
    std::string_view source_hint, std::string_view source_package_sha256) {
  SogouThemePackageConversion result;
  const SogouThemePackageEntryView* skin_ini = nullptr;
  for (const SogouThemePackageEntryView& entry : entries) {
    if (!IsRootSkinIni(entry.relative_path)) {
      continue;
    }
    if (skin_ini != nullptr) {
      AddIssue(&result.conversion, SogouThemeIssueCode::kDuplicateProperty, 0,
               "$.skin.ini",
               "SKIN_INI_AMBIGUOUS: package contains multiple root skin.ini entries");
      return result;
    }
    skin_ini = &entry;
  }
  if (skin_ini == nullptr) {
    AddIssue(&result.conversion, SogouThemeIssueCode::kMissingProperty, 0,
             "$.skin.ini",
             "SKIN_INI_MISSING: package has no root skin.ini entry");
    return result;
  }

  SogouThemeTextNormalization normalized =
      NormalizeSogouThemeIniText(skin_ini->bytes);
  result.skin_ini_encoding = normalized.encoding;
  if (!normalized.ok()) {
    AddIssue(&result.conversion, SogouThemeIssueCode::kInvalidEncoding, 0,
             "$.skin.ini", std::move(normalized.error));
    return result;
  }
  result.conversion = ConvertSogouThemeIni(
      normalized.utf8, source_hint, source_package_sha256);
  return result;
}

}  // namespace ziliu::core
