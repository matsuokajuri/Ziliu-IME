#include "ziliu/core/sogou_theme.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
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
  return (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z');
}

bool IsAsciiDigit(char value) { return value >= '0' && value <= '9'; }

char AsciiLower(char value) {
  return value >= 'A' && value <= 'Z' ? static_cast<char>(value - 'A' + 'a') : value;
}

std::string AsciiLowerCopy(std::string_view value) {
  std::string result(value);
  std::transform(result.begin(), result.end(), result.begin(), AsciiLower);
  return result;
}

std::string_view Trim(std::string_view value) {
  while (!value.empty() && (value.front() == ' ' || value.front() == '\t' ||
                            value.front() == '\r')) {
    value.remove_prefix(1);
  }
  while (!value.empty() && (value.back() == ' ' || value.back() == '\t' ||
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
    for (std::size_t continuation = 1; continuation <= continuation_count;
         ++continuation) {
      const auto byte = static_cast<unsigned char>(text[index + continuation]);
      if ((byte & 0xC0U) != 0x80U) {
        return false;
      }
      code_point = (code_point << 6U) | (byte & 0x3FU);
    }
    if (code_point < minimum || code_point > 0x10FFFFU ||
        (code_point >= 0xD800U && code_point <= 0xDFFFU)) {
      return false;
    }
    index += continuation_count + 1;
  }
  return true;
}

bool HasUnsupportedControlCharacter(std::string_view text) {
  return std::any_of(text.begin(), text.end(), [](char value) {
    const auto byte = static_cast<unsigned char>(value);
    return byte < 0x20U && value != '\r' && value != '\n' && value != '\t';
  });
}

bool IsIniIdentifier(std::string_view value) {
  if (value.empty()) {
    return false;
  }
  return std::all_of(value.begin(), value.end(), [](char character) {
    return IsAsciiAlpha(character) || IsAsciiDigit(character) || character == '_';
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
  if (HasUnsupportedControlCharacter(input)) {
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
    if (++line_number > kMaximumIniLines) {
      AddIssue(conversion, SogouThemeIssueCode::kMalformedIni, line_number, "$",
               "skin.ini contains too many lines");
      return std::nullopt;
    }
    const std::size_t end = input.find('\n', begin);
    const std::string_view raw_line =
        input.substr(begin, end == std::string_view::npos ? input.size() - begin
                                                          : end - begin);
    if (raw_line.size() > kMaximumIniLineBytes) {
      AddIssue(conversion, SogouThemeIssueCode::kMalformedIni, line_number, "$",
               "skin.ini line exceeds the 8192-byte limit");
      return std::nullopt;
    }
    const std::string_view line = Trim(raw_line);
    if (!line.empty() && line.front() != ';' && line.front() != '#') {
      if (line.front() == '[') {
        if (line.size() < 3 || line.back() != ']') {
          AddIssue(conversion, SogouThemeIssueCode::kMalformedIni, line_number,
                   "$", "section header is malformed");
          return std::nullopt;
        }
        const std::string_view section_name = Trim(line.substr(1, line.size() - 2));
        if (!IsIniIdentifier(section_name)) {
          AddIssue(conversion, SogouThemeIssueCode::kMalformedIni, line_number,
                   "$", "section name must use ASCII letters, digits or '_'");
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
          AddIssue(conversion, SogouThemeIssueCode::kMalformedIni, line_number,
                   "$", "property appears before the first section");
          return std::nullopt;
        }
        const std::size_t equals = line.find('=');
        if (equals == std::string_view::npos) {
          AddIssue(conversion, SogouThemeIssueCode::kMalformedIni, line_number,
                   current_section, "property must contain '='");
          return std::nullopt;
        }
        const std::string_view raw_key = Trim(line.substr(0, equals));
        if (!IsIniIdentifier(raw_key)) {
          AddIssue(conversion, SogouThemeIssueCode::kMalformedIni, line_number,
                   current_section,
                   "property name must use ASCII letters, digits or '_'");
          return std::nullopt;
        }
        if (++property_count > kMaximumIniProperties) {
          AddIssue(conversion, SogouThemeIssueCode::kMalformedIni, line_number,
                   "$", "skin.ini contains too many properties");
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
        const std::string_view raw_value = Trim(line.substr(equals + 1));
        section.emplace(key, IniValue{std::string(raw_value), line_number});
      }
    }

    if (end == std::string_view::npos) {
      break;
    }
    begin = end + 1;
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
    source_hint.remove_prefix(slash + 1);
  }
  if (source_hint.size() >= 4 &&
      AsciiLowerCopy(source_hint.substr(source_hint.size() - 4)) == ".ssf") {
    source_hint.remove_suffix(4);
  }
  source_hint = Trim(source_hint);
  return source_hint.empty() ? "导入的搜狗主题" : std::string(source_hint);
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
         (result.back() == '.' || result.back() == '_' || result.back() == '-')) {
    result.pop_back();
  }
  return result;
}

std::uint64_t StableHash(std::string_view input, std::string_view source_hint) {
  constexpr std::uint64_t kOffset = 14695981039346656037ULL;
  constexpr std::uint64_t kPrime = 1099511628211ULL;
  std::uint64_t hash = kOffset;
  const auto append = [&](std::string_view text, std::uint64_t* destination) {
    for (const char character : text) {
      *destination ^= static_cast<unsigned char>(character);
      *destination *= kPrime;
    }
  };
  append(input, &hash);
  hash ^= 0xFFU;
  hash *= kPrime;
  append(source_hint, &hash);
  return hash;
}

std::string Hex64(std::uint64_t value) {
  constexpr char kDigits[] = "0123456789abcdef";
  std::string result(16, '0');
  for (std::size_t index = 0; index < result.size(); ++index) {
    const std::size_t shift = (result.size() - index - 1) * 4;
    result[index] = kDigits[(value >> shift) & 0x0FU];
  }
  return result;
}

std::string MakeThemeId(const IniDocument& document, std::string_view input,
                        std::string_view source_hint,
                        std::string_view display_name) {
  if (const IniValue* skin_id =
          FindValue(document, "General", "skin_id");
      skin_id != nullptr && !skin_id->value.empty()) {
    std::string normalized = NormalizeIdComponent(skin_id->value, 120);
    if (!normalized.empty()) {
      return "sogou." + normalized;
    }
  }

  std::string slug = NormalizeIdComponent(display_name, 88);
  if (slug.empty()) {
    slug = "skin";
  }
  return "sogou." + slug + "-" + Hex64(StableHash(input, source_hint));
}

bool ParseUnsigned(std::string_view text, std::uint32_t maximum,
                   std::uint32_t* output) {
  text = Trim(text);
  if (text.empty() || text.front() == '-' || text.front() == '+') {
    return false;
  }
  int base = 10;
  if (text.size() > 2 && text[0] == '0' &&
      (text[1] == 'x' || text[1] == 'X')) {
    text.remove_prefix(2);
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

std::vector<std::string_view> SplitCommaPreservingEmpty(std::string_view value) {
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
    begin = end + 1;
  }
  return fields;
}

std::optional<std::vector<std::uint32_t>> ParseNumberList(
    const IniValue& value, std::size_t expected_count, std::uint32_t maximum,
    std::string path, SogouThemeConversion* conversion) {
  const std::vector<std::string_view> fields =
      SplitCommaPreservingEmpty(value.value);
  if (fields.size() != expected_count) {
    AddIssue(conversion, SogouThemeIssueCode::kInvalidValue, value.line,
             std::move(path), "property has the wrong number of comma-separated values");
    return std::nullopt;
  }
  std::vector<std::uint32_t> numbers;
  numbers.reserve(fields.size());
  for (const std::string_view field : fields) {
    std::uint32_t number = 0;
    if (!ParseUnsigned(field, maximum, &number)) {
      AddIssue(conversion, SogouThemeIssueCode::kInvalidValue, value.line,
               std::move(path), "property contains an invalid unsigned integer");
      return std::nullopt;
    }
    numbers.push_back(number);
  }
  return numbers;
}

std::optional<std::array<std::int32_t, 10>> ParseOverlayAlignment(
    const IniValue& value, std::string path,
    SogouThemeConversion* conversion) {
  const std::vector<std::string_view> fields =
      SplitCommaPreservingEmpty(value.value);
  if (fields.size() != 10) {
    AddIssue(conversion, SogouThemeIssueCode::kInvalidValue, value.line,
             std::move(path),
             "custom overlay alignment must contain exactly 10 integers");
    return std::nullopt;
  }

  std::array<std::int32_t, 10> alignment{};
  for (std::size_t index = 0; index < alignment.size(); ++index) {
    const std::string_view field = fields[index];
    if (field.empty()) {
      AddIssue(conversion, SogouThemeIssueCode::kInvalidValue, value.line,
               std::move(path),
               "custom overlay alignment contains an empty integer");
      return std::nullopt;
    }
    const auto parsed =
        std::from_chars(field.data(), field.data() + field.size(),
                        alignment[index]);
    if (parsed.ec != std::errc{} ||
        parsed.ptr != field.data() + field.size()) {
      AddIssue(conversion, SogouThemeIssueCode::kInvalidValue, value.line,
               std::move(path),
               "custom overlay alignment must use signed 32-bit integers");
      return std::nullopt;
    }
  }
  return alignment;
}

std::optional<ThemeImageLayout> MapLayoutMode(
    std::uint32_t mode, bool horizontal, const IniValue& value,
    std::string path, SogouThemeConversion* conversion) {
  if (mode == 0) {
    return ThemeImageLayout::kStretch;
  }
  if (mode == 1) {
    return ThemeImageLayout::kTile;
  }
  if (!horizontal && mode == 2) {
    return ThemeImageLayout::kFixed;
  }
  AddIssue(
      conversion, SogouThemeIssueCode::kInvalidValue, value.line,
      std::move(path),
      horizontal
          ? "horizontal layout mode must be 0 (stretch) or 1 (tile)"
          : "vertical layout mode must be 0 (stretch), 1 (tile) or 2 (fixed)");
  return std::nullopt;
}

std::optional<std::uint32_t> ParseBgrColor(
    const IniValue& value, std::string path,
    SogouThemeConversion* conversion) {
  std::uint32_t bgr = 0;
  if (!ParseUnsigned(value.value, 0xFFFFFFU, &bgr)) {
    AddIssue(conversion, SogouThemeIssueCode::kInvalidValue, value.line,
             std::move(path), "color must be a 24-bit decimal or hexadecimal BGR value");
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
  const std::string base =
      AsciiLowerCopy(component.substr(0, dot));
  if (base == "con" || base == "prn" || base == "aux" || base == "nul") {
    return true;
  }
  if (base.size() == 4 &&
      (base.starts_with("com") || base.starts_with("lpt")) &&
      base[3] >= '1' && base[3] <= '9') {
    return true;
  }
  return false;
}

std::optional<std::string> NormalizeSourceAssetPath(std::string_view path) {
  path = Trim(path);
  if (path.empty() || path.size() > 240 || path.front() == '/' ||
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
        std::any_of(component.begin(), component.end(), [](char value) {
          return static_cast<unsigned char>(value) < 0x20U;
        })) {
      return std::nullopt;
    }
    if (end == std::string::npos) {
      break;
    }
    begin = end + 1;
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
    const std::optional<std::string> normalized =
        NormalizeSourceAssetPath(source.value);
    if (!normalized.has_value()) {
      AddIssue(conversion_, SogouThemeIssueCode::kUnsafeAssetPath,
               source.line, std::move(path),
               "asset must be a safe relative PNG, APNG, BMP, JPG, JPEG or GIF path");
      return std::nullopt;
    }

    const std::string comparison_key = AsciiLowerCopy(*normalized);
    if (const auto existing = targets_by_source_.find(comparison_key);
        existing != targets_by_source_.end()) {
      return existing->second;
    }

    std::string index = std::to_string(conversion_->assets.size());
    if (index.size() < 3) {
      index.insert(index.begin(), 3 - index.size(), '0');
    }
    std::string target = "assets/ssf-" + index + ".png";
    conversion_->assets.push_back({*normalized, target});
    targets_by_source_.emplace(comparison_key, target);
    return target;
  }

 private:
  SogouThemeConversion* conversion_;
  std::map<std::string, std::string> targets_by_source_;
};

void MapMetadata(const IniDocument& document, std::string_view input,
                 std::string_view source_hint, AssetCollector* assets,
                 SogouThemeConversion* conversion) {
  ThemeManifest& manifest = conversion->manifest;
  const IniValue* name =
      FindFirstValue(document, "General", {"skin_name", "name"});
  const IniValue* author =
      FindFirstValue(document, "General", {"skin_author", "author"});
  const IniValue* version =
      FindFirstValue(document, "General", {"skin_version", "version"});
  const IniValue* description =
      FindFirstValue(document, "General", {"skin_info", "info"});

  // Real packages retain this complete authoring-template quartet even when the
  // distributed package has a different name. It is not package identity.
  // Require the whole quartet so a legitimate individual value such as "new"
  // is never discarded on its own.
  const bool has_authoring_template_metadata =
      name != nullptr && name->value == "new" && author != nullptr &&
      author->value == "匿名" && version != nullptr && version->value == "0.9" &&
      description != nullptr && description->value == "欢迎大家使用";
  manifest.name = name == nullptr || has_authoring_template_metadata
                      ? DisplayNameFromSourceHint(source_hint)
                      : name->value;
  manifest.id = MakeThemeId(document, input, source_hint, manifest.name);
  manifest.author = author == nullptr || has_authoring_template_metadata
                        ? "未提供"
                        : author->value;
  manifest.version = version == nullptr || has_authoring_template_metadata
                         ? "未提供"
                         : version->value;
  manifest.license = "LicenseRef-Unknown";
  manifest.description = description == nullptr || has_authoring_template_metadata
                             ? std::string()
                             : description->value;
  manifest.source_format = "sogou-ssf";
  manifest.base_dpi = 96;

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
  ThemeAppearance& appearance = conversion->manifest.light;
  // Sogou same-window skins distinguish the first candidate through its text
  // color. Keeping Ziliu's default blue selection fill would obscure the skin.
  appearance.palette.highlighted_background = 0x00000000U;
  if (const IniValue* flag = FindValue(document, "Display", "use_gdip"); flag != nullptr) {
    std::uint32_t parsed = 0;
    if (!ParseUnsigned(flag->value, 1, &parsed)) {
      AddIssue(conversion, SogouThemeIssueCode::kInvalidValue, flag->line,
               "Display.use_gdip", "use_gdip must be 0 or 1");
    } else {
      appearance.typography.sogou_use_gdip = parsed;
    }
  }
  if (const IniValue* font =
          FindValue(document, "Display", "font_ch");
      font != nullptr && !font->value.empty()) {
    appearance.typography.chinese_font_family = font->value;
  }
  if (const IniValue* font =
          FindValue(document, "Display", "font_en");
      font != nullptr && !font->value.empty()) {
    appearance.typography.english_font_family = font->value;
  }
  if (const IniValue* font_size =
          FindValue(document, "Display", "font_size");
      font_size != nullptr) {
    std::uint32_t parsed = 0;
    if (!ParseUnsigned(font_size->value, 96, &parsed) || parsed < 8) {
      AddIssue(conversion, SogouThemeIssueCode::kInvalidValue,
               font_size->line, "Display.font_size",
               "font_size must be between 8 and 96");
    } else {
      appearance.typography.font_size = parsed;
    }
  }

  const auto map_color = [&](std::string_view key, std::uint32_t* destination) {
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
  map_color("comphint_color", &appearance.palette.muted_text);
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
    if (!ParseUnsigned(display->value, 1, &enabled)) {
      AddIssue(conversion, SogouThemeIssueCode::kInvalidValue, display->line,
               std::string(section_name) + "." + display_key,
               "button display flag must be 0 or 1");
      return std::nullopt;
    }
    if (enabled == 0) {
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
  const std::vector<std::string_view> normal_fields =
      SplitCommaPreservingEmpty(normal->value);
  const auto first_normal = std::find_if(
      normal_fields.begin(), normal_fields.end(),
      [](std::string_view field) { return !field.empty(); });
  if (first_normal == normal_fields.end()) {
    AddIssue(conversion, SogouThemeIssueCode::kInvalidValue, normal->line,
             std::string(section_name) + "." + std::string(prefix),
             "button must provide at least one image");
    return std::nullopt;
  }

  IniValue normal_asset{std::string(*first_normal), normal->line};
  ThemeButtonImages button;
  if (const auto target = assets->Register(
          normal_asset, std::string(section_name) + "." + std::string(prefix));
      target.has_value()) {
    button.normal = *target;
  }
  const auto map_optional_state =
      [&](std::string_view suffix, std::string* destination) {
        const std::string key = std::string(prefix) + std::string(suffix);
        const IniValue* state = FindValue(document, section_name, key);
        if (state == nullptr) {
          return;
        }
        const std::vector<std::string_view> fields =
            SplitCommaPreservingEmpty(state->value);
        const auto first_state = std::find_if(
            fields.begin(), fields.end(),
            [](std::string_view field) { return !field.empty(); });
        if (first_state == fields.end()) {
          return;
        }
        IniValue state_asset{std::string(*first_state), state->line};
        if (const auto target = assets->Register(
                state_asset, std::string(section_name) + "." + key);
            target.has_value()) {
          *destination = *target;
        }
      };
  map_optional_state("_hover", &button.hover);
  map_optional_state("_down", &button.pressed);
  return button.normal.empty() ? std::nullopt
                               : std::optional<ThemeButtonImages>(button);
}

void MapCustomOverlay(const IniDocument& document,
                      std::string_view section_name,
                      std::uint32_t custom_index, ThemeSurface* surface,
                      AssetCollector* assets,
                      SogouThemeConversion* conversion) {
  const std::string prefix = "custom" + std::to_string(custom_index);
  const std::string display_key = prefix + "_display";
  const IniValue* display =
      FindValue(document, section_name, display_key);
  if (display == nullptr) {
    return;
  }

  std::uint32_t enabled = 0;
  if (!ParseUnsigned(display->value, 1, &enabled)) {
    AddIssue(conversion, SogouThemeIssueCode::kInvalidValue, display->line,
             std::string(section_name) + "." + display_key,
             "custom overlay display flag must be 0 or 1");
    return;
  }
  if (enabled == 0) {
    return;
  }

  const std::string asset_path =
      std::string(section_name) + "." + prefix;
  const IniValue* image = FindValue(document, section_name, prefix);
  if (image == nullptr || image->value.empty()) {
    AddIssue(conversion, SogouThemeIssueCode::kMissingProperty,
             image == nullptr ? 0 : image->line, asset_path,
             "enabled custom overlay must provide an image");
  }

  const std::string align_key = prefix + "_align";
  const std::string align_path =
      std::string(section_name) + "." + align_key;
  const IniValue* align = FindValue(document, section_name, align_key);
  if (align == nullptr) {
    AddIssue(conversion, SogouThemeIssueCode::kMissingProperty, 0, align_path,
             "enabled custom overlay must provide 10 alignment integers");
  }

  std::optional<std::string> target;
  if (image != nullptr && !image->value.empty()) {
    target = assets->Register(*image, asset_path);
  }
  std::optional<std::array<std::int32_t, 10>> alignment;
  if (align != nullptr) {
    alignment = ParseOverlayAlignment(*align, align_path, conversion);
  }
  if (target.has_value() && alignment.has_value()) {
    surface->overlays.push_back(
        ThemeOverlay{*target, custom_index, custom_index, *alignment});
  }
}

void MapCustomOverlays(const IniDocument& document,
                       std::string_view section_name, ThemeSurface* surface,
                       AssetCollector* assets,
                       SogouThemeConversion* conversion) {
  const IniValue* count =
      FindValue(document, section_name, "custom_cnt");
  if (count == nullptr) {
    return;
  }

  std::uint32_t parsed_count = 0;
  if (!ParseUnsigned(
          count->value,
          static_cast<std::uint32_t>(kMaximumThemeSurfaceOverlays),
          &parsed_count)) {
    AddIssue(conversion, SogouThemeIssueCode::kInvalidValue, count->line,
             std::string(section_name) + ".custom_cnt",
             "custom_cnt must be an integer between 0 and 128");
    return;
  }

  // Current SSF uses custom0..custom(count-1). Some legacy generators use
  // custom1..custom(count), so inspect the final one-based index as well. An
  // entry is still ignored unless its explicit display flag is 1.
  for (std::uint32_t index = 0; index < parsed_count; ++index) {
    MapCustomOverlay(document, section_name, index, surface, assets,
                     conversion);
  }
  if (parsed_count != 0) {
    MapCustomOverlay(document, section_name, parsed_count, surface, assets,
                     conversion);
  }
}

void MapSurface(const IniDocument& document, std::string_view section_name,
                ThemeSurface* surface, AssetCollector* assets,
                SogouThemeConversion* conversion) {
  if (FindSection(document, section_name) == nullptr) {
    return;
  }
  const IniValue* picture = FindValue(document, section_name, "pic");
  const bool missing_picture = picture == nullptr || picture->value.empty();
  if (missing_picture && section_name != "Scheme_V1") {
    AddIssue(conversion, SogouThemeIssueCode::kMissingProperty,
             picture == nullptr ? 0 : picture->line,
             std::string(section_name) + ".pic",
             "same-window scheme must provide a background picture");
  }

  ThemeImage background;
  bool has_background = false;
  if (picture != nullptr && !picture->value.empty()) {
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
    if (const auto values =
            ParseNumberList(*layout, 3, kMaximumMappedInset,
                            std::string(section_name) + ".layout_horizontal",
                            conversion);
        values.has_value()) {
      if (const auto mapped =
              MapLayoutMode((*values)[0], true, *layout,
                            std::string(section_name) + ".layout_horizontal",
                            conversion);
          mapped.has_value()) {
        background.horizontal_layout = *mapped;
      }
      background.stretch.left = (*values)[1];
      background.stretch.right = (*values)[2];
    }
  }
  if (const IniValue* layout =
          FindValue(document, section_name, "layout_vertical");
      layout != nullptr) {
    if (const auto values =
            ParseNumberList(*layout, 3, kMaximumMappedInset,
                            std::string(section_name) + ".layout_vertical",
                            conversion);
        values.has_value()) {
      if (const auto mapped =
              MapLayoutMode((*values)[0], false, *layout,
                            std::string(section_name) + ".layout_vertical",
                            conversion);
          mapped.has_value()) {
        background.vertical_layout = *mapped;
      }
      background.stretch.top = (*values)[1];
      background.stretch.bottom = (*values)[2];
    }
  }
  if (has_background) {
    surface->background = std::move(background);
  }
  MapCustomOverlays(document, section_name, surface, assets, conversion);

  const auto map_insets =
      [&](std::string_view key, std::optional<ThemeInsets>* destination) {
        const IniValue* value = FindValue(document, section_name, key);
        if (value == nullptr) {
          return;
        }
        if (const auto values =
                ParseNumberList(*value, 4, kMaximumMappedInset,
                                std::string(section_name) + "." +
                                    std::string(key),
                                conversion);
            values.has_value()) {
          // SSF stores top,bottom,left,right; ZLT stores left,top,right,bottom.
          *destination =
              ThemeInsets{(*values)[2], (*values)[0], (*values)[3], (*values)[1]};
        }
      };
  map_insets("pinyin_marge", &surface->preedit_insets);
  map_insets("zhongwen_marge", &surface->candidate_insets);

  if (const IniValue* separator =
          FindValue(document, section_name, "separator");
      separator != nullptr) {
    const std::vector<std::string_view> fields =
        SplitCommaPreservingEmpty(separator->value);
    if (fields.size() != 3) {
      AddIssue(conversion, SogouThemeIssueCode::kInvalidValue,
               separator->line, std::string(section_name) + ".separator",
               "separator must contain color,left,right");
    } else {
      IniValue color_value{std::string(fields[0]), separator->line};
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
                 "separator margins must be unsigned integers up to 4096");
      } else if (color.has_value()) {
        surface->separator = ThemeSeparator{*color, "", left, right, 1};
      }
    }
  }

  surface->previous_button =
      MapButton(document, section_name, "pageup", assets, conversion);
  surface->next_button =
      MapButton(document, section_name, "pagedown", assets, conversion);
  if (missing_picture && section_name == "Scheme_V1") {
    // A partial V1 is not a custom vertical surface. Keep validating its declared
    // fields and assets above, but leave no geometry for the native fallback.
    *surface = ThemeSurface{};
  }
}

void AppendManifestIssues(SogouThemeConversion* conversion) {
  const std::vector<ThemeManifestIssue> manifest_issues =
      ValidateThemeManifest(conversion->manifest);
  for (const ThemeManifestIssue& issue : manifest_issues) {
    AddIssue(conversion, SogouThemeIssueCode::kInvalidManifest, 0,
             issue.path, issue.message);
  }
}

}  // namespace

SogouThemeConversion ConvertSogouThemeIni(std::string_view utf8_ini,
                                          std::string_view source_hint) {
  SogouThemeConversion conversion;
  const std::optional<IniDocument> document =
      ParseIni(utf8_ini, &conversion);
  if (!document.has_value()) {
    return conversion;
  }

  AssetCollector assets(&conversion);
  MapMetadata(*document, utf8_ini, source_hint, &assets, &conversion);
  MapDisplay(*document, &conversion);

  const bool has_horizontal =
      FindSection(*document, "Scheme_H1") != nullptr;
  const bool has_vertical =
      FindSection(*document, "Scheme_V1") != nullptr;
  if (!has_horizontal && !has_vertical) {
    AddIssue(&conversion, SogouThemeIssueCode::kMissingProperty, 0, "$",
             "skin.ini must contain Scheme_H1 or Scheme_V1");
  }
  MapSurface(*document, "Scheme_H1",
             &conversion.manifest.light.horizontal, &assets, &conversion);
  MapSurface(*document, "Scheme_V1",
             &conversion.manifest.light.vertical, &assets, &conversion);
  if (conversion.ok() &&
      !conversion.manifest.light.horizontal.background.has_value() &&
      !conversion.manifest.light.vertical.background.has_value()) {
    AddIssue(&conversion, SogouThemeIssueCode::kMissingProperty, 0, "$",
             "skin.ini must provide an H1 or V1 background picture");
  }
  AppendManifestIssues(&conversion);
  return conversion;
}

}  // namespace ziliu::core
