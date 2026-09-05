#include "ziliu/core/sogou_theme.h"
#include "ziliu/ui/bitmap.h"

#ifdef _WIN32
#include "../src/settings/sogou_ssf_container.h"

#include <windows.h>
#include <bcrypt.h>
#endif

#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::string_view kPackageSha =
    "0123456789abcdef0123456789abcdef"
    "0123456789abcdef0123456789abcdef";

void Expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

bool HasIssue(const ziliu::core::SogouThemeConversion& conversion,
              ziliu::core::SogouThemeIssueCode code,
              std::string_view path = {}) {
  for (const auto& issue : conversion.issues) {
    if (issue.code == code && (path.empty() || issue.path == path)) {
      return true;
    }
  }
  return false;
}

bool HasSourceAsset(const ziliu::core::SogouThemeConversion& conversion,
                    std::string_view source) {
  for (const auto& asset : conversion.assets) {
    if (asset.source_path == source) {
      return true;
    }
  }
  return false;
}

std::vector<std::uint8_t> Bytes(std::string_view text) {
  std::vector<std::uint8_t> bytes;
  bytes.reserve(text.size());
  for (const char character : text) {
    bytes.push_back(
        static_cast<std::uint8_t>(static_cast<unsigned char>(character)));
  }
  return bytes;
}

struct OwnedPackageEntry {
  std::string relative_path;
  std::vector<std::uint8_t> bytes;
};

std::vector<ziliu::core::SogouThemePackageEntryView> EntryViews(
    const std::vector<OwnedPackageEntry>& entries) {
  std::vector<ziliu::core::SogouThemePackageEntryView> views;
  views.reserve(entries.size());
  for (const auto& entry : entries) {
    views.push_back({entry.relative_path,
                     std::span<const std::uint8_t>(entry.bytes)});
  }
  return views;
}

std::vector<std::uint8_t> Utf16LeBom(std::u16string_view text) {
  std::vector<std::uint8_t> bytes{0xFFU, 0xFEU};
  bytes.reserve(2U + text.size() * 2U);
  for (const char16_t code_unit : text) {
    bytes.push_back(static_cast<std::uint8_t>(code_unit));
    bytes.push_back(static_cast<std::uint8_t>(code_unit >> 8U));
  }
  return bytes;
}

ziliu::core::SogouThemeTextNormalization NormalizeBytes(
    std::initializer_list<std::uint8_t> bytes) {
  return ziliu::core::NormalizeSogouThemeIniText(
      std::span<const std::uint8_t>(bytes.begin(), bytes.size()));
}

ziliu::core::SogouThemePackageConversion BindSingleEntry(
    std::string_view path, const std::vector<std::uint8_t>& bytes) {
  const std::array entries = {
      ziliu::core::SogouThemePackageEntryView{
          path, std::span<const std::uint8_t>(bytes)},
  };
  return ziliu::core::ConvertSogouThemePackage(entries, "binding.ssf",
                                                kPackageSha);
}

#ifdef _WIN32
std::string_view EncodingName(ziliu::core::SogouThemeIniEncoding encoding) {
  switch (encoding) {
    case ziliu::core::SogouThemeIniEncoding::kUtf8:
      return "utf-8";
    case ziliu::core::SogouThemeIniEncoding::kUtf8Bom:
      return "utf-8-bom";
    case ziliu::core::SogouThemeIniEncoding::kUtf16LeBom:
      return "utf-16le-bom";
    case ziliu::core::SogouThemeIniEncoding::kUnknown:
      break;
  }
  return "unknown";
}

std::string SourceForTarget(
    const ziliu::core::SogouThemeConversion& conversion,
    std::string_view target) {
  for (const auto& asset : conversion.assets) {
    if (asset.target_path == target) {
      return asset.source_path;
    }
  }
  return {};
}

void AppendLe16(std::vector<std::uint8_t>& output, std::uint16_t value) {
  output.push_back(static_cast<std::uint8_t>(value));
  output.push_back(static_cast<std::uint8_t>(value >> 8U));
}

void AppendLe32(std::vector<std::uint8_t>& output, std::uint32_t value) {
  output.push_back(static_cast<std::uint8_t>(value));
  output.push_back(static_cast<std::uint8_t>(value >> 8U));
  output.push_back(static_cast<std::uint8_t>(value >> 16U));
  output.push_back(static_cast<std::uint8_t>(value >> 24U));
}

std::uint32_t Crc32(std::span<const std::uint8_t> bytes) {
  std::uint32_t crc = 0xFFFFFFFFU;
  for (const std::uint8_t byte : bytes) {
    crc ^= byte;
    for (unsigned bit = 0; bit < 8U; ++bit) {
      const std::uint32_t mask =
          0U - static_cast<std::uint32_t>(crc & 1U);
      crc = (crc >> 1U) ^ (0xEDB88320U & mask);
    }
  }
  return ~crc;
}

void AppendPath(std::vector<std::uint8_t>& output, std::string_view path) {
  for (const char character : path) {
    output.push_back(
        static_cast<std::uint8_t>(static_cast<unsigned char>(character)));
  }
}

std::vector<std::uint8_t> BuildStoredZip(
    std::span<const OwnedPackageEntry> entries) {
  Expect(entries.size() <= 0xFFFFU,
         "synthetic ZIP entry count should fit its fixed width");
  struct CentralRecord {
    const OwnedPackageEntry* entry = nullptr;
    std::uint32_t crc = 0;
    std::uint32_t local_offset = 0;
  };
  std::vector<CentralRecord> central_records;
  central_records.reserve(entries.size());
  std::vector<std::uint8_t> archive;
  for (const OwnedPackageEntry& entry : entries) {
    Expect(entry.relative_path.size() <= 0xFFFFU &&
               entry.bytes.size() <= 0xFFFFFFFFULL &&
               archive.size() <= 0xFFFFFFFFULL,
           "synthetic ZIP fields should fit their fixed widths");
    const auto size = static_cast<std::uint32_t>(entry.bytes.size());
    const auto path_size =
        static_cast<std::uint16_t>(entry.relative_path.size());
    const std::uint32_t crc = Crc32(entry.bytes);
    central_records.push_back(
        {&entry, crc, static_cast<std::uint32_t>(archive.size())});
    AppendLe32(archive, 0x04034B50U);
    AppendLe16(archive, 20U);
    AppendLe16(archive, 0U);
    AppendLe16(archive, 0U);
    AppendLe16(archive, 0U);
    AppendLe16(archive, 0U);
    AppendLe32(archive, crc);
    AppendLe32(archive, size);
    AppendLe32(archive, size);
    AppendLe16(archive, path_size);
    AppendLe16(archive, 0U);
    AppendPath(archive, entry.relative_path);
    archive.insert(archive.end(), entry.bytes.begin(), entry.bytes.end());
  }

  const auto central_offset = static_cast<std::uint32_t>(archive.size());
  for (const CentralRecord& record : central_records) {
    const auto size = static_cast<std::uint32_t>(record.entry->bytes.size());
    const auto path_size =
        static_cast<std::uint16_t>(record.entry->relative_path.size());
    AppendLe32(archive, 0x02014B50U);
    AppendLe16(archive, 0x0314U);
    AppendLe16(archive, 20U);
    AppendLe16(archive, 0U);
    AppendLe16(archive, 0U);
    AppendLe16(archive, 0U);
    AppendLe16(archive, 0U);
    AppendLe32(archive, record.crc);
    AppendLe32(archive, size);
    AppendLe32(archive, size);
    AppendLe16(archive, path_size);
    AppendLe16(archive, 0U);
    AppendLe16(archive, 0U);
    AppendLe16(archive, 0U);
    AppendLe16(archive, 0U);
    AppendLe32(archive, 0x81B60020U);
    AppendLe32(archive, record.local_offset);
    AppendPath(archive, record.entry->relative_path);
  }
  const auto central_size =
      static_cast<std::uint32_t>(archive.size()) - central_offset;
  const auto entry_count = static_cast<std::uint16_t>(entries.size());
  AppendLe32(archive, 0x06054B50U);
  AppendLe16(archive, 0U);
  AppendLe16(archive, 0U);
  AppendLe16(archive, entry_count);
  AppendLe16(archive, entry_count);
  AppendLe32(archive, central_size);
  AppendLe32(archive, central_offset);
  AppendLe16(archive, 0U);
  return archive;
}

std::vector<std::uint8_t> BuildStoredZip(std::string_view path,
                                         std::span<const std::uint8_t> bytes) {
  const std::vector<OwnedPackageEntry> entries = {
      {std::string(path), std::vector<std::uint8_t>(bytes.begin(), bytes.end())},
  };
  return BuildStoredZip(entries);
}

class TemporaryZip {
 public:
  explicit TemporaryZip(std::span<const std::uint8_t> bytes) {
    std::array<wchar_t, MAX_PATH> directory{};
    const DWORD length =
        GetTempPathW(static_cast<DWORD>(directory.size()), directory.data());
    Expect(length != 0U && length < directory.size(),
           "temporary directory should be available");
    std::array<wchar_t, MAX_PATH> path{};
    Expect(GetTempFileNameW(directory.data(), L"zsf", 0U, path.data()) != 0U,
           "temporary ZIP path should be available");
    path_ = path.data();
    std::ofstream output(path_, std::ios::binary | std::ios::trunc);
    Expect(output.good(), "temporary ZIP should open");
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    output.close();
    Expect(output.good(), "temporary ZIP should be written completely");
  }

  TemporaryZip(const TemporaryZip&) = delete;
  TemporaryZip& operator=(const TemporaryZip&) = delete;

  ~TemporaryZip() {
    std::error_code ignored;
    std::filesystem::remove(path_, ignored);
  }

  [[nodiscard]] const std::filesystem::path& path() const noexcept {
    return path_;
  }

 private:
  std::filesystem::path path_;
};

std::vector<ziliu::core::SogouThemePackageEntryView> EntryViews(
    const ziliu::settings::SogouSsfDecodeResult& decoded) {
  std::vector<ziliu::core::SogouThemePackageEntryView> views;
  views.reserve(decoded.entries.size());
  for (const auto& entry : decoded.entries) {
    views.push_back({entry.relative_path,
                     std::span<const std::uint8_t>(entry.bytes)});
  }
  return views;
}

std::string Sha256Bytes(std::span<const std::uint8_t> bytes) {
  BCRYPT_ALG_HANDLE algorithm = nullptr;
  BCRYPT_HASH_HANDLE hash = nullptr;
  Expect(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM,
                                     nullptr, 0) >= 0,
         "SHA-256 provider should open");
  Expect(BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) >= 0,
         "SHA-256 state should initialize");
  Expect(BCryptHashData(
             hash,
             bytes.empty()
                 ? nullptr
                 : const_cast<PUCHAR>(
                       reinterpret_cast<const UCHAR*>(bytes.data())),
             static_cast<ULONG>(bytes.size()), 0) >= 0,
         "SHA-256 input should be accepted");
  std::array<UCHAR, 32> digest{};
  Expect(BCryptFinishHash(hash, digest.data(),
                          static_cast<ULONG>(digest.size()), 0) >= 0,
         "SHA-256 digest should finish");
  BCryptDestroyHash(hash);
  BCryptCloseAlgorithmProvider(algorithm, 0);
  constexpr char kHex[] = "0123456789abcdef";
  std::string encoded;
  encoded.reserve(digest.size() * 2U);
  for (const UCHAR byte : digest) {
    encoded.push_back(kHex[byte >> 4U]);
    encoded.push_back(kHex[byte & 0x0FU]);
  }
  return encoded;
}

const ziliu::core::SogouThemeResolvedAsset* FindResolvedSource(
    const ziliu::core::SogouThemeResourceBinding& binding,
    std::string_view source_path) {
  for (const auto& asset : binding.assets) {
    if (asset.source_path == source_path) {
      return &asset;
    }
  }
  return nullptr;
}

int InspectRealSsf(const std::filesystem::path& path,
                   std::string_view expected_package_sha,
                   std::string_view expected_h1_sha,
                   std::string_view expected_v1_sha) {
  const auto package_sha = ziliu::settings::Sha256SogouSsfFile(path);
  const auto decoded = ziliu::settings::DecodeSogouSsf(path);
  if (!package_sha.has_value() || !decoded.ok()) {
    std::cerr << "FAILED: real package decode: " << decoded.error << '\n';
    return EXIT_FAILURE;
  }
  const auto views = EntryViews(decoded);
  const auto package = ziliu::core::ConvertSogouThemePackage(
      views, "selected.ssf", *package_sha);
  if (!package.ok()) {
    for (const auto& issue : package.conversion.issues) {
      std::cerr << "FAILED: " << issue.path << ": " << issue.message << '\n';
    }
    return EXIT_FAILURE;
  }
  const auto binding =
      ziliu::core::ResolveSogouThemePackageResources(package, views);
  if (!binding.ok() ||
      binding.assets.size() != package.conversion.assets.size()) {
    std::cerr << "FAILED: real package resource binding: " << binding.error
              << '\n';
    return EXIT_FAILURE;
  }
  const auto& manifest = package.conversion.manifest;
  const auto& appearance = manifest.appearance;
  const std::string horizontal_source =
      appearance.horizontal.has_value() &&
              appearance.horizontal->background.has_value()
          ? SourceForTarget(package.conversion,
                            appearance.horizontal->background->asset)
          : std::string{};
  const std::string vertical_source =
      appearance.vertical.has_value() &&
              appearance.vertical->background.has_value()
          ? SourceForTarget(package.conversion,
                            appearance.vertical->background->asset)
          : std::string{};
  const auto* horizontal_asset =
      FindResolvedSource(binding, horizontal_source);
  const auto* vertical_asset = FindResolvedSource(binding, vertical_source);
  if (horizontal_asset == nullptr || vertical_asset == nullptr) {
    std::cerr << "FAILED: real H1/V1 background bytes were not bound\n";
    return EXIT_FAILURE;
  }
  const std::string horizontal_sha = Sha256Bytes(horizontal_asset->bytes);
  const std::string vertical_sha = Sha256Bytes(vertical_asset->bytes);
  if ((!expected_package_sha.empty() && *package_sha != expected_package_sha) ||
      (!expected_h1_sha.empty() && horizontal_sha != expected_h1_sha) ||
      (!expected_v1_sha.empty() && vertical_sha != expected_v1_sha)) {
    std::cerr << "FAILED: real package or bound resource identity drifted\n";
    return EXIT_FAILURE;
  }
  const auto horizontal_bitmap = ziliu::ui::DecodePngBitmap(horizontal_asset->bytes);
  const auto vertical_bitmap = ziliu::ui::DecodePngBitmap(vertical_asset->bytes);
  if (!horizontal_bitmap.ok() || !vertical_bitmap.ok()) {
    std::cerr << "FAILED: bound PNG decode: h1="
              << static_cast<int>(horizontal_bitmap.error) << ':'
              << horizontal_bitmap.platform_error << " v1="
              << static_cast<int>(vertical_bitmap.error) << ':'
              << vertical_bitmap.platform_error << '\n';
    return EXIT_FAILURE;
  }
  std::string_view renderer = "unset";
  if (appearance.typography.text_renderer ==
      ziliu::core::ThemeTextRenderer::kSogouGdiPlus) {
    renderer = "gdiplus";
  } else if (appearance.typography.text_renderer ==
             ziliu::core::ThemeTextRenderer::kSogouGdi) {
    renderer = "gdi";
  }
  std::cout << "package_sha=" << *package_sha << '\n';
  std::cout << "container_kind="
            << (decoded.kind ==
                        ziliu::settings::SogouSsfContainerKind::kZip
                    ? "zip"
                    : "skin-v3")
            << '\n';
  std::cout << "skin_ini_encoding="
            << EncodingName(package.skin_ini_encoding) << '\n';
  std::cout << "name=" << manifest.name << '\n';
  std::cout << "version=" << manifest.version << '\n';
  std::cout << "author=" << manifest.author << '\n';
  std::cout << "h1_source=" << horizontal_source << '\n';
  std::cout << "v1_source=" << vertical_source << '\n';
  std::cout << "h1_bound_sha=" << horizontal_sha << '\n';
  std::cout << "v1_bound_sha=" << vertical_sha << '\n';
  std::cout << "bitmap_format=RGBA8_STRAIGHT_TOP_DOWN\n";
  std::cout << "h1_dimensions=" << horizontal_bitmap.bitmap.width << 'x'
            << horizontal_bitmap.bitmap.height << '\n';
  std::cout << "h1_stride=" << horizontal_bitmap.bitmap.stride << '\n';
  std::cout << "h1_pixels_sha=" << Sha256Bytes(horizontal_bitmap.bitmap.pixels) << '\n';
  std::cout << "v1_dimensions=" << vertical_bitmap.bitmap.width << 'x'
            << vertical_bitmap.bitmap.height << '\n';
  std::cout << "v1_stride=" << vertical_bitmap.bitmap.stride << '\n';
  std::cout << "v1_pixels_sha=" << Sha256Bytes(vertical_bitmap.bitmap.pixels) << '\n';
  std::cout << "manifest_asset_count=" << package.conversion.assets.size()
            << '\n';
  std::cout << "resolved_asset_count=" << binding.assets.size() << '\n';
  std::cout << "font=" << appearance.typography.chinese_font_family << '\n';
  std::cout << "font_size="
            << appearance.typography.font_size.value_or(0U) << '\n';
  std::cout << "text_renderer=" << renderer << '\n';
  return EXIT_SUCCESS;
}
#endif

constexpr std::string_view kCompleteIni = R"ini(
[General]
skin_id=Paper Boat
skin_name=纸舟
skin_author=Ziliu Tests
skin_version=2.4
skin_info=Custom same-window mapping
preview_square=preview.gif

[Display]
font_size=18
font_ch=思源黑体
font_en=Segoe UI
use_gdip=1
pinyin_color=0x0080FF
zhongwen_color=1122867
zhongwen_first_color=0x030201
comphint_color=0x665544
caret_color=0x102030
zhongwen_first_bk_color=0xA0B0C0

[Scheme_H1]
anchor=11,47
pic=images\horizontal.bmp
layout_horizontal=0,38,192
layout_vertical=2,33,11
pinyin_marge=47,10,25,120
zhongwen_marge=10,5,15,90
separator=0xd8d8d8,20,100
pageup_display=1
pageup=up.png,unused.png
pageup_hover=up-hover.png
pageup_down=up-pressed.png
pagedown_display=0
pagedown=../disabled.png

[Scheme_V1]
anchor=-6,66
pic=images/horizontal.bmp
layout_horizontal=1,4,5
layout_vertical=0,6,7
custom_cnt=1
custom0_display=1
custom0=overlay.png
custom0_align=0,-1,2,-3,4,-5,6,-7,8,-9

[Scheme_H2]
pic=../../ignored.bmp
[StatusBar]
pic=ignored.exe
)ini";

constexpr std::string_view kBindingIniUtf8 =
    "[General]\r\n"
    "skin_name=纸舟\r\n"
    "skin_version=0.9\r\n"
    "skin_author=匿名\r\n"
    "[Display]\r\n"
    "font_ch=荆南麦圆体\r\n"
    "font_size=24\r\n"
    "use_gdip=1\r\n"
    "[Scheme_H1]\r\n"
    "pic=skin1.png\r\n"
    "[Scheme_V1]\r\n"
    "pic=skin2.png\r\n";

constexpr std::u16string_view kBindingIniUtf16 =
    u"[General]\r\n"
    u"skin_name=纸舟\r\n"
    u"skin_version=0.9\r\n"
    u"skin_author=匿名\r\n"
    u"[Display]\r\n"
    u"font_ch=荆南麦圆体\r\n"
    u"font_size=24\r\n"
    u"use_gdip=1\r\n"
    u"[Scheme_H1]\r\n"
    u"pic=skin1.png\r\n"
    u"[Scheme_V1]\r\n"
    u"pic=skin2.png\r\n";

}  // namespace

#ifdef _WIN32
int wmain(int argument_count, wchar_t* arguments[]) {
  if (argument_count == 2 || argument_count == 5) {
    const auto narrow = [](const wchar_t* value) {
      std::string result;
      while (*value != L'\0') {
        Expect(*value <= 0x7F,
               "expected real-package identities should be ASCII");
        result.push_back(static_cast<char>(*value));
        ++value;
      }
      return result;
    };
    return InspectRealSsf(
        std::filesystem::path(arguments[1]),
        argument_count == 5 ? narrow(arguments[2]) : std::string{},
        argument_count == 5 ? narrow(arguments[3]) : std::string{},
        argument_count == 5 ? narrow(arguments[4]) : std::string{});
  }
  Expect(argument_count == 1 && arguments[0] != nullptr,
         "test executable accepts a real SSF path and optional identities");
#else
int main() {
#endif
  using namespace ziliu::core;

  const std::vector<std::uint8_t> utf8_bytes = Bytes(kBindingIniUtf8);
  std::vector<std::uint8_t> utf8_bom{0xEFU, 0xBBU, 0xBFU};
  utf8_bom.insert(utf8_bom.end(), utf8_bytes.begin(), utf8_bytes.end());
  const std::vector<std::uint8_t> utf16_bytes =
      Utf16LeBom(kBindingIniUtf16);
  const auto normalized_utf8 = NormalizeSogouThemeIniText(utf8_bytes);
  const auto normalized_utf8_bom = NormalizeSogouThemeIniText(utf8_bom);
  const auto normalized_utf16 = NormalizeSogouThemeIniText(utf16_bytes);
  Expect(normalized_utf8.ok() && normalized_utf8.utf8 == kBindingIniUtf8 &&
             normalized_utf8.encoding == SogouThemeIniEncoding::kUtf8,
         "strict UTF-8 skin.ini should pass unchanged");
  Expect(normalized_utf8_bom.ok() &&
             normalized_utf8_bom.utf8 == kBindingIniUtf8 &&
             normalized_utf8_bom.encoding ==
                 SogouThemeIniEncoding::kUtf8Bom,
         "UTF-8 BOM should be removed without changing text");
  Expect(normalized_utf16.ok() && normalized_utf16.utf8 == kBindingIniUtf8 &&
             normalized_utf16.encoding ==
                 SogouThemeIniEncoding::kUtf16LeBom,
         "strict UTF-16LE should normalize to equivalent UTF-8");

  const auto valid_surrogate =
      NormalizeSogouThemeIniText(Utf16LeBom(u"\xD83D\xDE00"));
  Expect(valid_surrogate.ok() &&
             valid_surrogate.utf8 == "\xF0\x9F\x98\x80",
         "paired UTF-16 surrogates should encode one scalar value");
  const auto odd_utf16 = NormalizeBytes({0xFFU, 0xFEU, 0x41U});
  Expect(!odd_utf16.ok() && odd_utf16.error.find("odd") != std::string::npos,
         "odd UTF-16LE byte length should fail closed");
  const auto high_surrogate =
      NormalizeBytes({0xFFU, 0xFEU, 0x00U, 0xD8U});
  Expect(!high_surrogate.ok() &&
             high_surrogate.error.find("high surrogate") != std::string::npos,
         "unpaired UTF-16 high surrogate should fail closed");
  const auto low_surrogate =
      NormalizeBytes({0xFFU, 0xFEU, 0x00U, 0xDCU});
  Expect(!low_surrogate.ok() &&
             low_surrogate.error.find("low surrogate") != std::string::npos,
         "unpaired UTF-16 low surrogate should fail closed");
  const auto utf16_be = NormalizeBytes({0xFEU, 0xFFU, 0x00U, 0x41U});
  Expect(!utf16_be.ok() &&
             utf16_be.error.find("unsupported") != std::string::npos,
         "UTF-16BE should remain explicitly unsupported");
  const auto utf32 = NormalizeBytes({0xFFU, 0xFEU, 0x00U, 0x00U, 0x41U,
                                     0x00U, 0x00U, 0x00U});
  Expect(!utf32.ok() && utf32.error.find("unsupported") != std::string::npos,
         "UTF-32 should remain explicitly unsupported");
  const auto invalid_utf8 = NormalizeBytes({0xC3U, 0x28U});
  Expect(!invalid_utf8.ok() &&
             invalid_utf8.error.find("UTF-8") != std::string::npos,
         "invalid UTF-8 should fail closed");
  const auto embedded_nul = NormalizeBytes({0x41U, 0x00U, 0x42U});
  Expect(!embedded_nul.ok() &&
             embedded_nul.error.find("embedded NUL") != std::string::npos,
         "embedded NUL should fail instead of truncating text");

  const auto package_utf8 = BindSingleEntry("skin.ini", utf8_bytes);
  const auto package_utf8_bom = BindSingleEntry("skin.ini", utf8_bom);
  const auto package_utf16 = BindSingleEntry("skin.ini", utf16_bytes);
  Expect(package_utf8.ok() && package_utf8_bom.ok() && package_utf16.ok(),
         "supported skin.ini encodings should reach the existing converter");
  Expect(package_utf8.conversion.manifest ==
                 package_utf8_bom.conversion.manifest &&
             package_utf8.conversion.manifest ==
                 package_utf16.conversion.manifest &&
             package_utf8.conversion.assets ==
                 package_utf8_bom.conversion.assets &&
             package_utf8.conversion.assets == package_utf16.conversion.assets,
         "UTF-8 and UTF-16LE packages should have identical semantics");
  Expect(package_utf16.conversion.manifest.source_package_sha256 ==
             kPackageSha,
         "binding should preserve original SSF package identity");
  const std::vector<std::uint8_t> invalid_package_bytes{0xC3U, 0x28U};
  const auto invalid_package =
      BindSingleEntry("skin.ini", invalid_package_bytes);
  Expect(!invalid_package.ok() &&
             HasIssue(invalid_package.conversion,
                      SogouThemeIssueCode::kInvalidEncoding, "$.skin.ini"),
         "package binding should report text encoding failures explicitly");

  const std::array<SogouThemePackageEntryView, 0> no_entries{};
  const auto missing =
      ConvertSogouThemePackage(no_entries, "missing.ssf", kPackageSha);
  Expect(!missing.ok() &&
             HasIssue(missing.conversion, SogouThemeIssueCode::kMissingProperty,
                      "$.skin.ini") &&
             missing.conversion.issues.front().message.find(
                 "SKIN_INI_MISSING") != std::string::npos,
         "missing root skin.ini should fail closed");
  const std::array nested_entries = {
      SogouThemePackageEntryView{
          "nested/skin.ini", std::span<const std::uint8_t>(utf8_bytes)},
  };
  const auto nested =
      ConvertSogouThemePackage(nested_entries, "nested.ssf", kPackageSha);
  Expect(!nested.ok() &&
             HasIssue(nested.conversion, SogouThemeIssueCode::kMissingProperty,
                      "$.skin.ini"),
         "nested skin.ini should not impersonate the root file");
  const std::array ambiguous_entries = {
      SogouThemePackageEntryView{
          "skin.ini", std::span<const std::uint8_t>(utf8_bytes)},
      SogouThemePackageEntryView{
          "SKIN.INI", std::span<const std::uint8_t>(utf8_bytes)},
  };
  const auto ambiguous = ConvertSogouThemePackage(
      ambiguous_entries, "ambiguous.ssf", kPackageSha);
  Expect(!ambiguous.ok() &&
             HasIssue(ambiguous.conversion,
                      SogouThemeIssueCode::kDuplicateProperty, "$.skin.ini") &&
             ambiguous.conversion.issues.front().message.find(
                 "SKIN_INI_AMBIGUOUS") != std::string::npos,
         "case-insensitive root skin.ini ambiguity should fail closed");

  const std::vector<OwnedPackageEntry> binding_entries = {
      {"skin.ini", utf8_bytes},
      {"skin1.png", {0x10U, 0x20U, 0x30U}},
      {"skin2.png", {}},
  };
  const auto binding_views = EntryViews(binding_entries);
  const auto binding_package = ConvertSogouThemePackage(
      binding_views, "resources.ssf", kPackageSha);
  const auto resource_binding =
      ResolveSogouThemePackageResources(binding_package, binding_views);
  Expect(binding_package.ok() && resource_binding.ok() &&
             resource_binding.source_package_sha256 == kPackageSha &&
             resource_binding.assets.size() == 2U &&
             resource_binding.assets[0].source_path == "skin1.png" &&
             resource_binding.assets[0].target_path ==
                 "assets/ssf-000.png" &&
             resource_binding.assets[0].bytes ==
                 std::vector<std::uint8_t>({0x10U, 0x20U, 0x30U}) &&
             resource_binding.assets[1].source_path == "skin2.png" &&
             resource_binding.assets[1].bytes.empty(),
         "resource binding should preserve manifest order, package identity, "
         "exact bytes and zero-byte resources");

  const std::vector<OwnedPackageEntry> missing_resource_entries = {
      {"skin.ini", utf8_bytes},
      {"skin1.png", {0x10U}},
  };
  const auto missing_resource_views = EntryViews(missing_resource_entries);
  const auto missing_resource_package = ConvertSogouThemePackage(
      missing_resource_views, "missing-resource.ssf", kPackageSha);
  const auto missing_resource = ResolveSogouThemePackageResources(
      missing_resource_package, missing_resource_views);
  Expect(!missing_resource.ok() && missing_resource.assets.empty() &&
             missing_resource.error.find("PACKAGE_RESOURCE_NOT_FOUND") !=
                 std::string::npos,
         "a missing manifest resource should fail without partial output");

  const std::vector<OwnedPackageEntry> case_collision_entries = {
      {"skin.ini", utf8_bytes},
      {"skin1.png", {0x10U}},
      {"SKIN1.PNG", {0x11U}},
      {"skin2.png", {0x20U}},
  };
  const auto case_collision_views = EntryViews(case_collision_entries);
  const auto case_collision_package = ConvertSogouThemePackage(
      case_collision_views, "case-collision.ssf", kPackageSha);
  const auto case_collision = ResolveSogouThemePackageResources(
      case_collision_package, case_collision_views);
  Expect(!case_collision.ok() && case_collision.assets.empty() &&
             case_collision.error.find("PACKAGE_RESOURCE_AMBIGUOUS") !=
                 std::string::npos,
         "Windows case-equivalent package resources should be ambiguous");

  const std::vector<std::uint8_t> nested_ini = Bytes(
      "[General]\nskin_name=Nested\n[Scheme_H1]\n"
      "pic=images/skin.png\n");
  const std::vector<OwnedPackageEntry> slash_collision_entries = {
      {"skin.ini", nested_ini},
      {"images/skin.png", {0x10U}},
      {"images\\skin.png", {0x11U}},
  };
  const auto slash_collision_views = EntryViews(slash_collision_entries);
  const auto slash_collision_package = ConvertSogouThemePackage(
      slash_collision_views, "slash-collision.ssf", kPackageSha);
  const auto slash_collision = ResolveSogouThemePackageResources(
      slash_collision_package, slash_collision_views);
  Expect(!slash_collision.ok() && slash_collision.assets.empty() &&
             slash_collision.error.find("PACKAGE_RESOURCE_AMBIGUOUS") !=
                 std::string::npos,
         "slash-normalized package resource collisions should fail closed");

  for (const std::string_view unsafe_path : {"../escape.png",
                                              "C:/absolute.png"}) {
    std::vector<OwnedPackageEntry> unsafe_entries = binding_entries;
    unsafe_entries.push_back({std::string(unsafe_path), {0x40U}});
    const auto unsafe_views = EntryViews(unsafe_entries);
    const auto unsafe_package = ConvertSogouThemePackage(
        unsafe_views, "unsafe-resource.ssf", kPackageSha);
    const auto unsafe_binding =
        ResolveSogouThemePackageResources(unsafe_package, unsafe_views);
    Expect(!unsafe_binding.ok() && unsafe_binding.assets.empty() &&
               unsafe_binding.error.find("PACKAGE_RESOURCE_PATH_INVALID") !=
                   std::string::npos,
           "unsafe decoded package paths should fail before resource output");
  }

  auto empty_source_package = binding_package;
  empty_source_package.conversion.assets.front().source_path.clear();
  const auto empty_source = ResolveSogouThemePackageResources(
      empty_source_package, binding_views);
  Expect(!empty_source.ok() && empty_source.assets.empty() &&
             empty_source.error.find("PACKAGE_RESOURCE_PATH_INVALID") !=
                 std::string::npos,
         "an empty manifest source path should fail closed");

  auto duplicate_target_package = binding_package;
  duplicate_target_package.conversion.assets[1].target_path =
      duplicate_target_package.conversion.assets[0].target_path;
  const auto duplicate_target = ResolveSogouThemePackageResources(
      duplicate_target_package, binding_views);
  Expect(!duplicate_target.ok() && duplicate_target.assets.empty() &&
             duplicate_target.error.find("THEME_TARGET_ASSET_COLLISION") !=
                 std::string::npos,
         "two manifest assets must not share one target path");

  const std::vector<OwnedPackageEntry> reordered_entries = {
      binding_entries[2], binding_entries[0], binding_entries[1]};
  const auto reordered_views = EntryViews(reordered_entries);
  const auto reordered_package = ConvertSogouThemePackage(
      reordered_views, "reordered.ssf", kPackageSha);
  const auto reordered_binding = ResolveSogouThemePackageResources(
      reordered_package, reordered_views);
  Expect(reordered_binding.ok() &&
             reordered_binding.assets == resource_binding.assets,
         "decoded entry order must not change manifest resource order");

#ifdef _WIN32
  const auto verify_decoded_zip = [&](const std::vector<std::uint8_t>& ini,
                                      SogouThemeIniEncoding encoding) {
    const std::vector<OwnedPackageEntry> archive_entries = {
        {"skin.ini", ini},
        {"skin1.png", {0x01U, 0x02U, 0x03U}},
        {"skin2.png", {0xA0U, 0xB0U}},
    };
    const std::vector<std::uint8_t> archive = BuildStoredZip(archive_entries);
    TemporaryZip file(archive);
    const auto package_sha =
        ziliu::settings::Sha256SogouSsfFile(file.path());
    const auto decoded = ziliu::settings::DecodeSogouSsf(file.path());
    Expect(package_sha.has_value() && decoded.ok() &&
               decoded.kind ==
                   ziliu::settings::SogouSsfContainerKind::kZip,
           "synthetic ZIP should use the product container decoder");
    const auto views = EntryViews(decoded);
    const auto bound = ConvertSogouThemePackage(
        views, "decoded.ssf", *package_sha);
    const auto resources = ResolveSogouThemePackageResources(bound, views);
    Expect(bound.ok() && bound.skin_ini_encoding == encoding &&
               bound.conversion.manifest.appearance.horizontal.has_value() &&
               bound.conversion.manifest.appearance.vertical.has_value() &&
               resources.ok() && resources.source_package_sha256 == *package_sha &&
               resources.assets.size() == 2U &&
               resources.assets[0].bytes == archive_entries[1].bytes &&
               resources.assets[1].bytes == archive_entries[2].bytes,
           "decoded ZIP should bind H1/V1 bytes through the full product chain");
  };
  verify_decoded_zip(utf8_bytes, SogouThemeIniEncoding::kUtf8);
  verify_decoded_zip(utf16_bytes, SogouThemeIniEncoding::kUtf16LeBom);

  ziliu::settings::SogouSsfDecodeResult skin_v3_decoded;
  skin_v3_decoded.kind =
      ziliu::settings::SogouSsfContainerKind::kSkinV3;
  skin_v3_decoded.entries = {
      {"skin.ini", utf8_bytes},
      {"skin1.png", {0x31U, 0x32U}},
      {"skin2.png", {0x41U, 0x42U}},
  };
  const auto skin_v3_views = EntryViews(skin_v3_decoded);
  const auto skin_v3_package = ConvertSogouThemePackage(
      skin_v3_views, "skin-v3.ssf", kPackageSha);
  const auto skin_v3_binding = ResolveSogouThemePackageResources(
      skin_v3_package, skin_v3_views);
  Expect(skin_v3_binding.ok() && skin_v3_binding.assets.size() == 2U &&
             skin_v3_binding.assets[0].bytes ==
                 skin_v3_decoded.entries[1].bytes &&
             skin_v3_binding.assets[1].bytes ==
                 skin_v3_decoded.entries[2].bytes,
         "resource binding must not depend on the decoded container kind");
#endif

  const auto conversion =
      ConvertSogouThemeIni(kCompleteIni, "paper-boat.ssf", kPackageSha);
  Expect(conversion.ok(), "valid H1 and V1 custom data should convert");
  Expect(conversion.manifest.id == "sogou.paper-boat" &&
             conversion.manifest.name == "纸舟" &&
             conversion.manifest.author == "Ziliu Tests" &&
             conversion.manifest.version == "2.4" &&
             conversion.manifest.source_package_sha256 == kPackageSha &&
             conversion.manifest.source_format == "sogou-ssf" &&
             conversion.manifest.base_dpi == 96U,
         "source metadata and identity should map without platform state");
  const auto& appearance = conversion.manifest.appearance;
  Expect(appearance.typography.chinese_font_family == "思源黑体" &&
             appearance.typography.english_font_family == "Segoe UI" &&
             appearance.typography.font_size == 18U &&
             appearance.typography.text_renderer ==
                 ThemeTextRenderer::kSogouGdiPlus,
         "font metadata should map exactly when present");
  Expect(appearance.palette.preedit_text == 0xFFFF8000U &&
             appearance.palette.candidate_text == 0xFF332211U &&
             appearance.palette.highlighted_candidate_text == 0xFF010203U &&
             appearance.palette.annotation_text == 0xFF445566U &&
             appearance.palette.caret_text == 0xFF302010U &&
             appearance.palette.highlighted_background == 0xFFC0B0A0U,
         "24-bit BGR values should map to opaque ARGB roles");

  Expect(appearance.horizontal.has_value() &&
             appearance.horizontal->anchor == ThemePoint{11, 47} &&
             appearance.horizontal->background.has_value() &&
             appearance.horizontal->background->horizontal_layout ==
                 ThemeImageLayout::kStretch &&
             appearance.horizontal->background->vertical_layout ==
                 ThemeImageLayout::kFixed &&
             appearance.horizontal->background->stretch ==
                 ThemeInsets{38, 33, 192, 11} &&
             appearance.horizontal->preedit_insets ==
                 ThemeInsets{25, 47, 120, 10} &&
             appearance.horizontal->candidate_insets ==
                 ThemeInsets{15, 10, 90, 5},
         "H1 anchor, image layout and margins should map independently");
  Expect(appearance.horizontal->separator.has_value() &&
             appearance.horizontal->separator->color == 0xFFD8D8D8U &&
             appearance.horizontal->separator->left == 20U &&
             appearance.horizontal->separator->right == 100U &&
             appearance.horizontal->previous_button.has_value() &&
             !appearance.horizontal->next_button.has_value(),
         "separator and enabled pager resources should map without synthesis");

  Expect(appearance.vertical.has_value() &&
             appearance.vertical->anchor == ThemePoint{-6, 66} &&
             appearance.vertical->background.has_value() &&
             appearance.vertical->background->horizontal_layout ==
                 ThemeImageLayout::kTile &&
             appearance.vertical->background->vertical_layout ==
                 ThemeImageLayout::kStretch &&
             appearance.vertical->overlays.size() == 1U &&
             appearance.vertical->overlays.front().raw_alignment ==
                 std::array<std::int32_t, 10>{
                     0, -1, 2, -3, 4, -5, 6, -7, 8, -9},
         "V1 and opaque custom alignment data should remain lossless");
  Expect(conversion.assets.size() == 6U &&
             conversion.assets.front().source_path == "preview.gif" &&
             conversion.assets.front().target_path == "assets/ssf-000.png" &&
             !HasSourceAsset(conversion, "../../ignored.bmp") &&
             !HasSourceAsset(conversion, "ignored.exe"),
         "only referenced same-window assets should receive controlled paths");
  Expect(ValidateThemeManifest(conversion.manifest).empty(),
         "converted custom SSF data should satisfy the manifest boundary");

  const auto sparse = ConvertSogouThemeIni(
      "[General]\nskin_name=Sparse\n[Scheme_V1]\n", "sparse.ssf",
      kPackageSha);
  Expect(sparse.ok() && sparse.manifest.appearance.vertical.has_value() &&
             !sparse.manifest.appearance.vertical->background.has_value() &&
             !sparse.manifest.appearance.horizontal.has_value() &&
             !sparse.manifest.appearance.typography.font_size.has_value() &&
             !sparse.manifest.appearance.typography.text_renderer.has_value() &&
             !sparse.manifest.appearance.palette.preedit_text.has_value() &&
             !sparse.manifest.appearance.palette.highlighted_background
                  .has_value(),
         "missing custom fields must remain unset without injected appearance");

  const auto missing_layout = ConvertSogouThemeIni(
      "[General]\nskin_name=No Layout\n[Scheme_H1]\npic=skin.png\n",
      "no-layout.ssf", kPackageSha);
  Expect(missing_layout.ok() &&
             missing_layout.manifest.appearance.horizontal->background
                 .has_value() &&
             !missing_layout.manifest.appearance.horizontal->background
                  ->horizontal_layout.has_value() &&
             !missing_layout.manifest.appearance.horizontal->background
                  ->vertical_layout.has_value(),
         "missing image layout fields must not acquire guessed modes");

  const auto bad_number = ConvertSogouThemeIni(
      "[General]\nskin_name=Bad\n[Display]\nfont_size=large\n"
      "[Scheme_H1]\npic=skin.png\n",
      "bad.ssf", kPackageSha);
  Expect(!bad_number.ok() &&
             HasIssue(bad_number, SogouThemeIssueCode::kInvalidValue,
                      "Display.font_size"),
         "invalid numeric values should carry their source path");

  const auto bad_path = ConvertSogouThemeIni(
      "[General]\nskin_name=Bad Path\n[Scheme_H1]\npic=../skin.png\n",
      "bad-path.ssf", kPackageSha);
  Expect(!bad_path.ok() &&
             HasIssue(bad_path, SogouThemeIssueCode::kUnsafeAssetPath,
                      "Scheme_H1.pic"),
         "malicious image paths should be rejected");

  const auto bad_identity = ConvertSogouThemeIni(
      "[General]\nskin_name=Bad Identity\n[Scheme_H1]\n",
      "bad-identity.ssf", "not-a-sha");
  Expect(!bad_identity.ok() &&
             HasIssue(bad_identity, SogouThemeIssueCode::kInvalidValue,
                      "$.source_package_sha256"),
         "package identity must be an exact SHA-256 digest");

  const auto no_scheme = ConvertSogouThemeIni(
      "[General]\nskin_name=No Scheme\n", "no-scheme.ssf", kPackageSha);
  Expect(!no_scheme.ok() &&
             HasIssue(no_scheme, SogouThemeIssueCode::kMissingProperty, "$"),
         "H1 or V1 is required for a custom candidate surface");

  const auto duplicate = ConvertSogouThemeIni(
      "[General]\nskin_name=A\nSKIN_NAME=B\n[Scheme_H1]\n",
      "duplicate.ssf", kPackageSha);
  Expect(!duplicate.ok() &&
             HasIssue(duplicate, SogouThemeIssueCode::kDuplicateProperty),
         "case-insensitive duplicate properties should be rejected");

  return EXIT_SUCCESS;
}
