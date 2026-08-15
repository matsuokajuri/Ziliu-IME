#include "../src/settings/sogou_ssf_container.h"
#include "ziliu/core/sogou_theme.h"

#include <Windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#pragma comment(lib, "bcrypt.lib")

namespace {

constexpr std::array<std::uint8_t, 32> kKey = {
    0x52, 0x36, 0x46, 0x1A, 0xD3, 0x85, 0x03, 0x66,
    0x90, 0x45, 0x16, 0x28, 0x79, 0x03, 0x36, 0x23,
    0xDD, 0xBE, 0x6F, 0x03, 0xFF, 0x04, 0xE3, 0xCA,
    0xD5, 0x7F, 0xFC, 0xA3, 0x50, 0xE4, 0x9E, 0xD9,
};

constexpr std::array<std::uint8_t, 16> kIv = {
    0xE0, 0x7A, 0xAD, 0x35, 0xE0, 0x90, 0xAA, 0x03,
    0x8A, 0x51, 0xFD, 0x05, 0xDF, 0x8C, 0x5D, 0x0F,
};

void Expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

void AppendLe32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
  bytes.push_back(static_cast<std::uint8_t>(value));
  bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
  bytes.push_back(static_cast<std::uint8_t>(value >> 16U));
  bytes.push_back(static_cast<std::uint8_t>(value >> 24U));
}

void AppendLe16(std::vector<std::uint8_t>& bytes, std::uint16_t value) {
  bytes.push_back(static_cast<std::uint8_t>(value));
  bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
}

void SetLe32(std::vector<std::uint8_t>& bytes, std::size_t offset,
             std::uint32_t value) {
  Expect(offset + 4U <= bytes.size(), "test LE32 write should be in bounds");
  bytes[offset] = static_cast<std::uint8_t>(value);
  bytes[offset + 1U] = static_cast<std::uint8_t>(value >> 8U);
  bytes[offset + 2U] = static_cast<std::uint8_t>(value >> 16U);
  bytes[offset + 3U] = static_cast<std::uint8_t>(value >> 24U);
}

std::uint32_t ReadLe32ForTest(std::span<const std::uint8_t> bytes,
                              std::size_t offset) {
  Expect(offset + 4U <= bytes.size(), "test LE32 read should be in bounds");
  return static_cast<std::uint32_t>(bytes[offset]) |
         (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U) |
         (static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U) |
         (static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U);
}

struct TestEntry {
  std::u16string path;
  std::vector<std::uint8_t> bytes;
};

std::vector<std::uint8_t> BuildBlob(std::span<const TestEntry> entries) {
  Expect(entries.size() <=
             static_cast<std::size_t>((std::numeric_limits<std::uint32_t>::max)() / 4U),
         "test entry count should fit the offset table");

  std::vector<std::vector<std::uint8_t>> records;
  records.reserve(entries.size());
  for (const TestEntry& entry : entries) {
    std::vector<std::uint8_t> record;
    const std::size_t filename_bytes = entry.path.size() * 2U;
    Expect(filename_bytes <= (std::numeric_limits<std::uint32_t>::max)(),
           "test filename should fit u32");
    AppendLe32(record, static_cast<std::uint32_t>(filename_bytes));
    for (const char16_t code_unit : entry.path) {
      record.push_back(static_cast<std::uint8_t>(code_unit));
      record.push_back(static_cast<std::uint8_t>(code_unit >> 8U));
    }
    Expect(entry.bytes.size() <= (std::numeric_limits<std::uint32_t>::max)(),
           "test content should fit u32");
    AppendLe32(record, static_cast<std::uint32_t>(entry.bytes.size()));
    record.insert(record.end(), entry.bytes.begin(), entry.bytes.end());
    records.push_back(std::move(record));
  }

  std::vector<std::uint8_t> blob;
  AppendLe32(blob, 0);
  AppendLe32(blob, static_cast<std::uint32_t>(entries.size() * 4U));
  std::size_t next_offset = 8U + entries.size() * 4U;
  for (const auto& record : records) {
    Expect(next_offset <= (std::numeric_limits<std::uint32_t>::max)(),
           "test record offset should fit u32");
    AppendLe32(blob, static_cast<std::uint32_t>(next_offset));
    next_offset += record.size();
  }
  for (const auto& record : records) {
    blob.insert(blob.end(), record.begin(), record.end());
  }
  Expect(blob.size() <= (std::numeric_limits<std::uint32_t>::max)(),
         "test blob should fit u32");
  SetLe32(blob, 0, static_cast<std::uint32_t>(blob.size()));
  return blob;
}

std::uint32_t Adler32(std::span<const std::uint8_t> bytes) {
  constexpr std::uint32_t kModulus = 65521;
  std::uint32_t first = 1;
  std::uint32_t second = 0;
  for (const std::uint8_t byte : bytes) {
    first = (first + byte) % kModulus;
    second = (second + first) % kModulus;
  }
  return (second << 16U) | first;
}

std::vector<std::uint8_t> MakeStoredZlib(
    std::span<const std::uint8_t> uncompressed) {
  Expect(uncompressed.size() <= (std::numeric_limits<std::uint16_t>::max)(),
         "stored zlib test vector should fit one DEFLATE block");
  const auto length = static_cast<std::uint16_t>(uncompressed.size());
  const auto complement = static_cast<std::uint16_t>(~length);

  std::vector<std::uint8_t> zlib = {
      0x78, 0x01, 0x01, static_cast<std::uint8_t>(length),
      static_cast<std::uint8_t>(length >> 8U),
      static_cast<std::uint8_t>(complement),
      static_cast<std::uint8_t>(complement >> 8U),
  };
  zlib.insert(zlib.end(), uncompressed.begin(), uncompressed.end());
  const std::uint32_t checksum = Adler32(uncompressed);
  zlib.push_back(static_cast<std::uint8_t>(checksum >> 24U));
  zlib.push_back(static_cast<std::uint8_t>(checksum >> 16U));
  zlib.push_back(static_cast<std::uint8_t>(checksum >> 8U));
  zlib.push_back(static_cast<std::uint8_t>(checksum));
  return zlib;
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

std::vector<std::uint8_t> MakeStoredDeflate(
    std::span<const std::uint8_t> uncompressed) {
  const std::vector<std::uint8_t> zlib = MakeStoredZlib(uncompressed);
  Expect(zlib.size() >= 6U, "test zlib should contain a wrapper");
  return std::vector<std::uint8_t>(zlib.begin() + 2, zlib.end() - 4);
}

std::vector<std::uint8_t> Utf16LeBytes(std::u16string_view text) {
  std::vector<std::uint8_t> bytes{0xFFU, 0xFEU};
  bytes.reserve(2U + text.size() * 2U);
  for (const char16_t character : text) {
    bytes.push_back(static_cast<std::uint8_t>(character));
    bytes.push_back(static_cast<std::uint8_t>(character >> 8U));
  }
  return bytes;
}

struct ZipTestEntry {
  std::string path;
  std::vector<std::uint8_t> bytes;
  std::uint16_t method = 0;
  std::uint16_t flags = 0;
  std::uint32_t external_attributes = 0x81B60020U;
  std::optional<std::uint32_t> declared_compressed_bytes;
  std::optional<std::uint32_t> declared_uncompressed_bytes;
  std::optional<std::uint32_t> declared_crc32;
};

struct PreparedZipEntry {
  const ZipTestEntry* source = nullptr;
  std::vector<std::uint8_t> compressed;
  std::uint32_t local_offset = 0;
  std::uint32_t compressed_bytes = 0;
  std::uint32_t uncompressed_bytes = 0;
  std::uint32_t crc32 = 0;
};

std::vector<std::uint8_t> BuildZip(
    std::span<const ZipTestEntry> entries) {
  Expect(entries.size() <= (std::numeric_limits<std::uint16_t>::max)(),
         "test ZIP entry count should fit u16");
  std::vector<std::uint8_t> archive;
  std::vector<PreparedZipEntry> prepared;
  prepared.reserve(entries.size());
  for (const ZipTestEntry& source : entries) {
    Expect(!source.path.empty() &&
               source.path.size() <=
                   (std::numeric_limits<std::uint16_t>::max)(),
           "test ZIP path should fit u16");
    PreparedZipEntry item;
    item.source = &source;
    item.compressed = source.method == 8U
                          ? MakeStoredDeflate(source.bytes)
                          : source.bytes;
    Expect(archive.size() <= (std::numeric_limits<std::uint32_t>::max)() &&
               item.compressed.size() <=
                   (std::numeric_limits<std::uint32_t>::max)() &&
               source.bytes.size() <=
                   (std::numeric_limits<std::uint32_t>::max)(),
           "test ZIP sizes should fit u32");
    item.local_offset = static_cast<std::uint32_t>(archive.size());
    item.compressed_bytes = source.declared_compressed_bytes.value_or(
        static_cast<std::uint32_t>(item.compressed.size()));
    item.uncompressed_bytes = source.declared_uncompressed_bytes.value_or(
        static_cast<std::uint32_t>(source.bytes.size()));
    item.crc32 = source.declared_crc32.value_or(Crc32(source.bytes));

    AppendLe32(archive, 0x04034B50U);
    AppendLe16(archive, 20U);
    AppendLe16(archive, source.flags);
    AppendLe16(archive, source.method);
    AppendLe16(archive, 0U);
    AppendLe16(archive, 0U);
    AppendLe32(archive, item.crc32);
    AppendLe32(archive, item.compressed_bytes);
    AppendLe32(archive, item.uncompressed_bytes);
    AppendLe16(archive, static_cast<std::uint16_t>(source.path.size()));
    AppendLe16(archive, 0U);
    for (const char character : source.path) {
      archive.push_back(
          static_cast<std::uint8_t>(static_cast<unsigned char>(character)));
    }
    archive.insert(archive.end(), item.compressed.begin(), item.compressed.end());
    prepared.push_back(std::move(item));
  }

  Expect(archive.size() <= (std::numeric_limits<std::uint32_t>::max)(),
         "test central offset should fit u32");
  const std::uint32_t central_offset =
      static_cast<std::uint32_t>(archive.size());
  for (const PreparedZipEntry& item : prepared) {
    const ZipTestEntry& source = *item.source;
    AppendLe32(archive, 0x02014B50U);
    AppendLe16(archive, 0x0314U);
    AppendLe16(archive, 20U);
    AppendLe16(archive, source.flags);
    AppendLe16(archive, source.method);
    AppendLe16(archive, 0U);
    AppendLe16(archive, 0U);
    AppendLe32(archive, item.crc32);
    AppendLe32(archive, item.compressed_bytes);
    AppendLe32(archive, item.uncompressed_bytes);
    AppendLe16(archive, static_cast<std::uint16_t>(source.path.size()));
    AppendLe16(archive, 0U);
    AppendLe16(archive, 0U);
    AppendLe16(archive, 0U);
    AppendLe16(archive, 0U);
    AppendLe32(archive, source.external_attributes);
    AppendLe32(archive, item.local_offset);
    for (const char character : source.path) {
      archive.push_back(
          static_cast<std::uint8_t>(static_cast<unsigned char>(character)));
    }
  }
  Expect(archive.size() - central_offset <=
             (std::numeric_limits<std::uint32_t>::max)(),
         "test central directory size should fit u32");
  const std::uint32_t central_bytes =
      static_cast<std::uint32_t>(archive.size() - central_offset);
  AppendLe32(archive, 0x06054B50U);
  AppendLe16(archive, 0U);
  AppendLe16(archive, 0U);
  AppendLe16(archive, static_cast<std::uint16_t>(entries.size()));
  AppendLe16(archive, static_cast<std::uint16_t>(entries.size()));
  AppendLe32(archive, central_bytes);
  AppendLe32(archive, central_offset);
  AppendLe16(archive, 0U);
  return archive;
}

std::uint8_t HexNibble(char character) {
  if (character >= '0' && character <= '9') {
    return static_cast<std::uint8_t>(character - '0');
  }
  if (character >= 'a' && character <= 'f') {
    return static_cast<std::uint8_t>(character - 'a' + 10);
  }
  if (character >= 'A' && character <= 'F') {
    return static_cast<std::uint8_t>(character - 'A' + 10);
  }
  Expect(false, "embedded test vector should contain only hexadecimal digits");
  return 0;
}

std::vector<std::uint8_t> ParseHex(std::string_view hex) {
  Expect(hex.size() % 2U == 0, "embedded test vector should have even hex length");
  std::vector<std::uint8_t> bytes;
  bytes.reserve(hex.size() / 2U);
  for (std::size_t index = 0; index < hex.size(); index += 2U) {
    bytes.push_back(static_cast<std::uint8_t>(
        (HexNibble(hex[index]) << 4U) | HexNibble(hex[index + 1U])));
  }
  return bytes;
}

struct AlgorithmHandle {
  BCRYPT_ALG_HANDLE value = nullptr;
  ~AlgorithmHandle() {
    if (value != nullptr) {
      BCryptCloseAlgorithmProvider(value, 0);
    }
  }
};

struct KeyHandle {
  BCRYPT_KEY_HANDLE value = nullptr;
  ~KeyHandle() {
    if (value != nullptr) {
      BCryptDestroyKey(value);
    }
  }
};

std::vector<std::uint8_t> Encrypt(std::span<const std::uint8_t> plaintext) {
  Expect(plaintext.size() <= (std::numeric_limits<ULONG>::max)(),
         "test plaintext should fit CNG");
  AlgorithmHandle algorithm;
  Expect(BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(
             &algorithm.value, BCRYPT_AES_ALGORITHM, nullptr, 0)),
         "test AES provider should open");
  Expect(BCRYPT_SUCCESS(BCryptSetProperty(
             algorithm.value, BCRYPT_CHAINING_MODE,
             reinterpret_cast<PUCHAR>(
                 const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_CBC)),
             static_cast<ULONG>(sizeof(BCRYPT_CHAIN_MODE_CBC)), 0)),
         "test AES provider should accept CBC mode");

  ULONG object_bytes = 0;
  ULONG returned_bytes = 0;
  Expect(BCRYPT_SUCCESS(BCryptGetProperty(
             algorithm.value, BCRYPT_OBJECT_LENGTH,
             reinterpret_cast<PUCHAR>(&object_bytes), sizeof(object_bytes),
             &returned_bytes, 0)) &&
             returned_bytes == sizeof(object_bytes),
         "test AES provider should report key object size");
  std::vector<std::uint8_t> key_object(object_bytes);
  KeyHandle key;
  Expect(BCRYPT_SUCCESS(BCryptGenerateSymmetricKey(
             algorithm.value, &key.value, key_object.data(), object_bytes,
             const_cast<PUCHAR>(kKey.data()), static_cast<ULONG>(kKey.size()), 0)),
         "test SSF AES key should import");

  ULONG required_bytes = 0;
  auto query_iv = kIv;
  Expect(BCRYPT_SUCCESS(BCryptEncrypt(
             key.value, const_cast<PUCHAR>(plaintext.data()),
             static_cast<ULONG>(plaintext.size()), nullptr, query_iv.data(),
             static_cast<ULONG>(query_iv.size()), nullptr, 0, &required_bytes,
             BCRYPT_BLOCK_PADDING)),
         "test AES encryption size query should succeed");
  std::vector<std::uint8_t> ciphertext(required_bytes);
  ULONG ciphertext_bytes = 0;
  auto encrypt_iv = kIv;
  Expect(BCRYPT_SUCCESS(BCryptEncrypt(
             key.value, const_cast<PUCHAR>(plaintext.data()),
             static_cast<ULONG>(plaintext.size()), nullptr, encrypt_iv.data(),
             static_cast<ULONG>(encrypt_iv.size()), ciphertext.data(),
             static_cast<ULONG>(ciphertext.size()), &ciphertext_bytes,
             BCRYPT_BLOCK_PADDING)),
         "test AES encryption should succeed");
  ciphertext.resize(ciphertext_bytes);
  return ciphertext;
}

struct HashHandle {
  BCRYPT_HASH_HANDLE value = nullptr;
  ~HashHandle() {
    if (value != nullptr) {
      BCryptDestroyHash(value);
    }
  }
};

std::string Sha256Hex(std::span<const std::uint8_t> bytes) {
  Expect(bytes.size() <= (std::numeric_limits<ULONG>::max)(),
         "test SHA input should fit CNG");
  AlgorithmHandle algorithm;
  Expect(BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(
             &algorithm.value, BCRYPT_SHA256_ALGORITHM, nullptr, 0)),
         "test SHA provider should open");
  ULONG object_bytes = 0;
  ULONG returned_bytes = 0;
  Expect(BCRYPT_SUCCESS(BCryptGetProperty(
             algorithm.value, BCRYPT_OBJECT_LENGTH,
             reinterpret_cast<PUCHAR>(&object_bytes), sizeof(object_bytes),
             &returned_bytes, 0)) &&
             returned_bytes == sizeof(object_bytes) && object_bytes != 0U,
         "test SHA provider should report object size");
  std::vector<std::uint8_t> object(object_bytes);
  HashHandle hash;
  Expect(BCRYPT_SUCCESS(BCryptCreateHash(
             algorithm.value, &hash.value, object.data(), object_bytes, nullptr,
             0, 0)),
         "test SHA hash should initialize");
  Expect(BCRYPT_SUCCESS(BCryptHashData(
             hash.value, const_cast<PUCHAR>(bytes.data()),
             static_cast<ULONG>(bytes.size()), 0)),
         "test SHA data should hash");
  std::array<std::uint8_t, 32> digest{};
  Expect(BCRYPT_SUCCESS(BCryptFinishHash(
             hash.value, digest.data(), static_cast<ULONG>(digest.size()), 0)),
         "test SHA digest should finish");
  constexpr char kHex[] = "0123456789abcdef";
  std::string result(digest.size() * 2U, '0');
  for (std::size_t index = 0; index < digest.size(); ++index) {
    result[index * 2U] = kHex[digest[index] >> 4U];
    result[index * 2U + 1U] = kHex[digest[index] & 0x0FU];
  }
  return result;
}

class TemporarySsf {
 public:
  explicit TemporarySsf(std::span<const std::uint8_t> archive) {
    std::array<wchar_t, MAX_PATH + 1> directory{};
    const DWORD directory_length =
        GetTempPathW(static_cast<DWORD>(directory.size()), directory.data());
    Expect(directory_length != 0 && directory_length < directory.size(),
           "test temporary directory should be available");
    std::array<wchar_t, MAX_PATH + 1> filename{};
    Expect(GetTempFileNameW(directory.data(), L"zsf", 0, filename.data()) != 0,
           "test temporary SSF path should be available");
    path_ = filename.data();

    HANDLE file = CreateFileW(path_.c_str(), GENERIC_WRITE, 0, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
    Expect(file != INVALID_HANDLE_VALUE, "test SSF file should open for writing");
    DWORD written = 0;
    const bool write_ok =
        WriteFile(file, archive.data(), static_cast<DWORD>(archive.size()),
                  &written, nullptr) != FALSE;
    CloseHandle(file);
    Expect(write_ok && written == archive.size(),
           "test SSF archive should be written completely");
  }

  TemporarySsf(const TemporarySsf&) = delete;
  TemporarySsf& operator=(const TemporarySsf&) = delete;

  TemporarySsf(TemporarySsf&& other) noexcept
      : path_(std::exchange(other.path_, {})) {}

  TemporarySsf& operator=(TemporarySsf&&) = delete;

  ~TemporarySsf() {
    if (!path_.empty()) {
      DeleteFileW(path_.c_str());
    }
  }

  [[nodiscard]] const std::wstring& path() const noexcept { return path_; }

 private:
  std::wstring path_;
};

TemporarySsf WriteSsf(std::span<const std::uint8_t> zlib,
                      std::size_t inflated_size,
                      std::uint32_t version = 3,
                      bool damage_padding = false) {
  Expect(inflated_size <= (std::numeric_limits<std::uint32_t>::max)(),
         "test inflated size should fit u32");
  std::vector<std::uint8_t> plaintext;
  AppendLe32(plaintext, static_cast<std::uint32_t>(inflated_size));
  plaintext.insert(plaintext.end(), zlib.begin(), zlib.end());
  std::vector<std::uint8_t> ciphertext = Encrypt(plaintext);
  if (damage_padding) {
    ciphertext.back() ^= 0x01U;
  }

  std::vector<std::uint8_t> archive{'S', 'k', 'i', 'n'};
  AppendLe32(archive, version);
  archive.insert(archive.end(), ciphertext.begin(), ciphertext.end());
  return TemporarySsf(archive);
}

void ExpectZipRejected(std::vector<std::uint8_t> archive,
                       std::string_view error_fragment,
                       std::string_view description) {
  TemporarySsf file(archive);
  const auto decoded = ziliu::settings::DecodeSogouSsf(file.path());
  if (decoded.ok() || decoded.error.find(error_fragment) == std::string::npos) {
    std::cerr << "FAILED: " << description << ": " << decoded.error << '\n';
    std::exit(EXIT_FAILURE);
  }
}

std::string_view ContainerKindName(
    ziliu::settings::SogouSsfContainerKind kind) {
  switch (kind) {
    case ziliu::settings::SogouSsfContainerKind::kSkinV3:
      return "skin-v3";
    case ziliu::settings::SogouSsfContainerKind::kZip:
      return "zip";
    case ziliu::settings::SogouSsfContainerKind::kUnknown:
      break;
  }
  return "unknown";
}

std::vector<std::uint8_t> BuildLargeContent() {
  constexpr std::string_view kPhrase =
      "the quick brown fox jumps over the lazy dog. the quick fox. ";
  std::vector<std::uint8_t> content;
  content.reserve(kPhrase.size() * 500U);
  for (std::size_t repetition = 0; repetition < 500U; ++repetition) {
    content.insert(content.end(), kPhrase.begin(), kPhrase.end());
  }
  return content;
}

void VerifyCompressedVector(std::string_view vector_hex,
                            std::string_view vector_kind) {
  const std::vector<std::uint8_t> content = BuildLargeContent();
  const std::array entries = {
      TestEntry{u"assets/test.bin", content},
  };
  const std::vector<std::uint8_t> blob = BuildBlob(entries);
  const std::vector<std::uint8_t> zlib = ParseHex(vector_hex);
  TemporarySsf file = WriteSsf(zlib, blob.size());
  const auto decoded = ziliu::settings::DecodeSogouSsfV3(file.path());
  if (!decoded.ok()) {
    std::cerr << "FAILED: " << vector_kind << " vector (" << zlib.size()
              << " zlib bytes, " << blob.size() << " output bytes): "
              << decoded.error << '\n';
    std::exit(EXIT_FAILURE);
  }
  Expect(decoded.entries.size() == 1,
         "compressed vector should decode exactly one entry");
  Expect(decoded.entries.front().relative_path == "assets/test.bin",
         "compressed vector should preserve its relative path");
  Expect(decoded.entries.front().bytes == content,
         "compressed vector should preserve its content");
}

}  // namespace

int wmain(int argument_count, wchar_t* arguments[]) {
  if (argument_count == 4 && std::wstring_view(arguments[1]) == L"--extract") {
    const auto decoded =
        ziliu::settings::DecodeSogouSsf(std::filesystem::path(arguments[2]));
    if (!decoded.ok()) {
      std::cerr << "FAILED: real SSF: " << decoded.error << '\n';
      return EXIT_FAILURE;
    }
    const std::filesystem::path output_root(arguments[3]);
    std::error_code filesystem_error;
    std::filesystem::create_directories(output_root, filesystem_error);
    if (filesystem_error) {
      std::cerr << "FAILED: could not create extraction root\n";
      return EXIT_FAILURE;
    }
    for (const auto& entry : decoded.entries) {
      const int wide_length = MultiByteToWideChar(
          CP_UTF8, MB_ERR_INVALID_CHARS, entry.relative_path.data(),
          static_cast<int>(entry.relative_path.size()), nullptr, 0);
      if (wide_length <= 0) {
        std::cerr << "FAILED: decoded SSF path is not UTF-8\n";
        return EXIT_FAILURE;
      }
      std::wstring wide_path(static_cast<std::size_t>(wide_length), L'\0');
      if (MultiByteToWideChar(
              CP_UTF8, MB_ERR_INVALID_CHARS, entry.relative_path.data(),
              static_cast<int>(entry.relative_path.size()), wide_path.data(),
              wide_length) != wide_length) {
        std::cerr << "FAILED: could not convert decoded SSF path\n";
        return EXIT_FAILURE;
      }
      const std::filesystem::path destination =
          output_root / std::filesystem::path(wide_path);
      std::filesystem::create_directories(destination.parent_path(),
                                          filesystem_error);
      if (filesystem_error) {
        std::cerr << "FAILED: could not create extraction directory\n";
        return EXIT_FAILURE;
      }
      std::ofstream output(destination, std::ios::binary | std::ios::trunc);
      if (!output ||
          !output.write(
              reinterpret_cast<const char*>(entry.bytes.data()),
              static_cast<std::streamsize>(entry.bytes.size()))) {
        std::cerr << "FAILED: could not write extracted SSF entry\n";
        return EXIT_FAILURE;
      }
    }
    std::cout << "container kind: " << ContainerKindName(decoded.kind) << '\n';
    std::cout << "extracted entries: " << decoded.entries.size() << '\n';
    return EXIT_SUCCESS;
  }
  if (argument_count == 2) {
    const auto decoded =
        ziliu::settings::DecodeSogouSsf(std::filesystem::path(arguments[1]));
    if (!decoded.ok()) {
      std::cerr << "FAILED: real SSF: " << decoded.error << '\n';
      return EXIT_FAILURE;
    }
    std::cout << "container kind: " << ContainerKindName(decoded.kind) << '\n';
    std::size_t total_bytes = 0;
    for (const auto& entry : decoded.entries) {
      total_bytes += entry.bytes.size();
      std::cout << entry.relative_path << '\t' << entry.bytes.size() << '\n';
    }
    std::cout << "decoded entries: " << decoded.entries.size()
              << ", total content bytes: " << total_bytes << '\n';
    return EXIT_SUCCESS;
  }
  Expect(argument_count == 1 && arguments[0] != nullptr,
          "test executable should receive no arguments, one SSF path, or "
          "--extract <SSF path> <output directory>");

  {
    const std::vector<std::uint8_t> skin_ini = Utf16LeBytes(
        u"[General]\r\nskin_name=Zip Fixture\r\n[Scheme_H1]\r\npic=skin1.png\r\n");
    const std::array entries = {
        ZipTestEntry{"skin.ini", skin_ini, 8U},
        ZipTestEntry{"skin1.png", {0x89U, 0x50U, 0x4EU, 0x47U}, 0U},
    };
    const std::vector<std::uint8_t> archive = BuildZip(entries);
    const std::string expected_sha = Sha256Hex(archive);
    TemporarySsf file(archive);
    const auto decoded = ziliu::settings::DecodeSogouSsf(file.path());
    const auto package_sha = ziliu::settings::Sha256SogouSsfFile(file.path());
    Expect(decoded.ok() &&
               decoded.kind == ziliu::settings::SogouSsfContainerKind::kZip,
           "generic decoder should recognize a PK/ZIP SSF");
    Expect(package_sha.has_value() && *package_sha == expected_sha,
           "ZIP package identity should hash the original SSF bytes");
    Expect(decoded.entries.size() == 2U &&
               decoded.entries[0] ==
                   ziliu::settings::SogouSsfEntry{"skin.ini", skin_ini} &&
               decoded.entries[1] == ziliu::settings::SogouSsfEntry{
                                         "skin1.png",
                                         {0x89U, 0x50U, 0x4EU, 0x47U}},
           "ZIP stored and raw-DEFLATE entries should round-trip exactly");
    const auto v3_only = ziliu::settings::DecodeSogouSsfV3(file.path());
    Expect(!v3_only.ok() && v3_only.error.find("Skin") != std::string::npos,
           "Skin-v3-specific API should not silently accept ZIP");
  }

  {
    const std::array entries = {
        ZipTestEntry{"assets/", {}, 0U, 0U, 0x41ED0010U},
        ZipTestEntry{"assets/skin.png", {1U, 2U, 3U}, 8U},
    };
    TemporarySsf file(BuildZip(entries));
    const auto decoded = ziliu::settings::DecodeSogouSsf(file.path());
    Expect(decoded.ok() && decoded.entries.size() == 1U &&
               decoded.entries.front().relative_path == "assets/skin.png",
            "safe ZIP directory entries should not become extracted files");
  }

  {
    const std::string utf8_path =
        "assets/\xE5\x80\x99\xE9\x80\x89.png";
    const std::array entries = {
        ZipTestEntry{utf8_path, {1U, 2U}, 0U, 0x0800U},
    };
    TemporarySsf file(BuildZip(entries));
    const auto decoded = ziliu::settings::DecodeSogouSsf(file.path());
    Expect(decoded.ok() && decoded.entries.size() == 1U &&
               decoded.entries.front().relative_path == utf8_path,
           "ZIP UTF-8 entry names should round-trip exactly");
  }

  {
    const std::array entries = {
        ZipTestEntry{"../evil.txt", {1U}, 0U},
    };
    ExpectZipRejected(BuildZip(entries), "上级目录", "ZIP traversal");
  }

  {
    const std::array entries = {
        ZipTestEntry{"/evil.txt", {1U}, 0U},
    };
    ExpectZipRejected(BuildZip(entries), "绝对路径", "ZIP absolute path");
  }

  {
    const std::array entries = {
        ZipTestEntry{"assets\\skin.png", {1U}, 0U},
        ZipTestEntry{"assets/skin.png", {2U}, 0U},
    };
    ExpectZipRejected(BuildZip(entries), "冲突",
                      "ZIP separator normalization collision");
  }

  {
    ZipTestEntry entry{"large.bin", {}, 8U};
    entry.declared_uncompressed_bytes = 8U * 1024U * 1024U + 1U;
    const std::array entries = {entry};
    ExpectZipRejected(BuildZip(entries), "8 MiB", "ZIP oversized entry");
  }

  {
    std::vector<ZipTestEntry> entries;
    for (unsigned index = 0; index < 5U; ++index) {
      ZipTestEntry entry{"total" + std::to_string(index) + ".bin", {}, 8U};
      entry.declared_uncompressed_bytes = 8U * 1024U * 1024U;
      entries.push_back(std::move(entry));
    }
    ExpectZipRejected(BuildZip(entries), "32 MiB",
                      "ZIP excessive total output");
  }

  {
    const std::array entries = {ZipTestEntry{"valid.txt", {1U}, 0U}};
    std::vector<std::uint8_t> archive = BuildZip(entries);
    const std::size_t end_offset = archive.size() - 22U;
    const std::size_t central_offset = ReadLe32ForTest(archive, end_offset + 16U);
    SetLe32(archive, central_offset, 0U);
    ExpectZipRejected(std::move(archive), "central directory",
                      "ZIP malformed central directory");
  }

  {
    const std::array entries = {
        ZipTestEntry{"encrypted.txt", {1U}, 0U, 0x0001U},
    };
    ExpectZipRejected(BuildZip(entries), "加密", "ZIP encryption");
  }

  {
    const std::array entries = {
        ZipTestEntry{"method.bin", {1U}, 99U},
    };
    ExpectZipRejected(BuildZip(entries), "压缩方法",
                      "ZIP unsupported compression method");
  }

  {
    const std::array entries = {
        ZipTestEntry{"descriptor.bin", {1U}, 0U, 0x0008U},
    };
    ExpectZipRejected(BuildZip(entries), "data descriptor",
                      "ZIP data descriptor");
  }

  {
    ZipTestEntry entry{"zip64.bin", {1U}, 0U};
    entry.declared_compressed_bytes = 0xFFFFFFFFU;
    const std::array entries = {entry};
    ExpectZipRejected(BuildZip(entries), "ZIP64", "ZIP64 sentinel");
  }

  {
    ZipTestEntry entry{"crc.bin", {1U, 2U, 3U}, 0U};
    entry.declared_crc32 = 0U;
    const std::array entries = {entry};
    ExpectZipRejected(BuildZip(entries), "CRC-32", "ZIP CRC mismatch");
  }

  {
    const std::array entries = {
        TestEntry{u"assets/候选.png", {0x89, 0x50, 0x4E, 0x47}},
    };
    const auto blob = BuildBlob(entries);
    const auto zlib = MakeStoredZlib(blob);
    TemporarySsf file = WriteSsf(zlib, blob.size());
    const auto decoded = ziliu::settings::DecodeSogouSsf(file.path());
    if (!decoded.ok()) {
      std::cerr << "FAILED: stored DEFLATE vector: " << decoded.error << '\n';
      return EXIT_FAILURE;
    }
    Expect(decoded.kind ==
               ziliu::settings::SogouSsfContainerKind::kSkinV3,
           "generic decoder should preserve Skin-v3 dispatch");
    Expect(decoded.entries.size() == 1,
           "stored DEFLATE SSF should contain one entry");
    Expect(decoded.entries.front().relative_path == "assets/候选.png",
           "UTF-16 filename should become UTF-8 with forward slashes");
    Expect(decoded.entries.front().bytes == entries.front().bytes,
           "stored DEFLATE entry bytes should round-trip");
  }

  {
    constexpr std::string_view kSkinIni =
        "[General]\n"
        "skin_name=Pipeline\n"
        "[Display]\n"
        "font_ch=Test Sans\n"
        "font_size=19\n"
        "pinyin_color=0x0080FF\n"
        "zhongwen_color=0x030201\n"
        "[Scheme_H1]\n"
        "pic=images/background.png\n"
        "layout_horizontal=0,8,9\n"
        "layout_vertical=1,10,11\n"
        "pinyin_marge=1,2,3,4\n"
        "zhongwen_marge=5,6,7,8\n";
    const std::array entries = {
        TestEntry{u"skin.ini",
                  std::vector<std::uint8_t>(kSkinIni.begin(), kSkinIni.end())},
        TestEntry{u"images/background.png", {0x89, 0x50, 0x4E, 0x47}},
    };
    const auto blob = BuildBlob(entries);
    TemporarySsf file = WriteSsf(MakeStoredZlib(blob), blob.size());
    const auto package_sha =
        ziliu::settings::Sha256SogouSsfFile(file.path());
    const auto decoded = ziliu::settings::DecodeSogouSsfV3(file.path());
    Expect(package_sha.has_value() && decoded.ok(),
           "synthetic custom SSF should decode with a package identity");
    const auto skin_entry = std::find_if(
        decoded.entries.begin(), decoded.entries.end(),
        [](const ziliu::settings::SogouSsfEntry& entry) {
          return entry.relative_path == "skin.ini";
        });
    Expect(skin_entry != decoded.entries.end(),
           "decoded custom SSF should expose skin.ini");
    const std::string skin_ini(
        reinterpret_cast<const char*>(skin_entry->bytes.data()),
        skin_entry->bytes.size());
    const auto conversion = ziliu::core::ConvertSogouThemeIni(
        skin_ini, "pipeline.ssf", *package_sha);
    Expect(conversion.ok() &&
               conversion.manifest.source_package_sha256 == *package_sha &&
               conversion.manifest.id ==
                   "sogou.pipeline-" + package_sha->substr(0, 16U),
           "decoded package identity should bind the converted manifest");
    Expect(conversion.manifest.appearance.horizontal.has_value() &&
               !conversion.manifest.appearance.vertical.has_value() &&
               conversion.manifest.appearance.horizontal->background
                   .has_value() &&
               conversion.manifest.appearance.horizontal->background->asset ==
                   "assets/ssf-000.png" &&
               conversion.manifest.appearance.horizontal->background
                       ->horizontal_layout ==
                   ziliu::core::ThemeImageLayout::kStretch &&
               conversion.manifest.appearance.horizontal->background
                       ->vertical_layout ==
                   ziliu::core::ThemeImageLayout::kTile,
           "decoded H1 should reach the custom manifest without another surface");
    Expect(conversion.manifest.appearance.typography.chinese_font_family ==
                   "Test Sans" &&
               conversion.manifest.appearance.typography.font_size == 19U &&
               conversion.manifest.appearance.palette.preedit_text ==
                   0xFFFF8000U &&
               conversion.manifest.appearance.palette.candidate_text ==
                   0xFF010203U &&
               conversion.manifest.appearance.horizontal->preedit_insets ==
                   ziliu::core::ThemeInsets{3, 1, 4, 2} &&
               conversion.manifest.appearance.horizontal->candidate_insets ==
                   ziliu::core::ThemeInsets{7, 5, 8, 6},
           "font, colors and margins should survive decode-to-model mapping");
    Expect(conversion.assets.size() == 1U &&
               conversion.assets.front().source_path ==
                   "images/background.png" &&
               std::any_of(decoded.entries.begin(), decoded.entries.end(),
                           [&](const ziliu::settings::SogouSsfEntry& entry) {
                             return entry.relative_path ==
                                    conversion.assets.front().source_path;
                           }) &&
               !conversion.manifest.appearance.palette.highlighted_background
                    .has_value(),
           "resource references should resolve to decoded entries without appearance injection");
  }

  constexpr std::string_view kFixedZlib =
      "78014b2a656060616060e0016239204e642806c254861220a90f2453817409831e"
      "43124326431e830150754946aa4261696672b64252517e799e425a7e854256696e"
      "41b1427e596a9102483a27b1aa5221253f5d4f01a118a80c993baa7754efa8de5"
      "1bda37a47f58eea1dd53baa7754efa8de51bda37a47f58eea1dd53baa7754efa"
      "8de51bda37a47f58eea1dd53baa7754efa8de51bda37a47f58eea1dd53baa775"
      "4efa8de51bda37a47f58eea1dd53baa7754efa8de51bda37a47f58eea1dd53ba"
      "a7754efa8de51bda37a47f58eea1dd53baa7754efa8de51bda37a47f58eea1dd"
      "53baa7754efa8de51bda37a47f58eea1dd53baa7754efa8de51bda37a47f58ee"
      "a1dd53baa7754efa8de51bda37a47f58eea1dd53baa7754efa8de51bda37a47f"
      "58eea1dd53baa7754efa8de51bda37a47f58eea1dd53baa7754efa8de51bda37"
      "a47f58eea1dd53baa7754efa8de51bd7a0a000529080a";
  VerifyCompressedVector(kFixedZlib, "fixed Huffman");

  constexpr std::string_view kDynamicZlib =
      "78daedcbc90dc2500c05401f3852037205819608841d0259d8aae77382224696"
      "6d597e538f11938898969e955e465faa89a1cc79994dd9435451c73e2eb128e9"
      "61d7e46ddcaf8e5977ede3929bf69987f17cedb3bd375d7edfa7e5fb95eb765b"
      "e52f5c62ff27cbb22ccbb22ccbb22ccbb22ccbb22ccbb22ccbb22ccbb22ccbb2"
      "2ccbb22ccbb22ccbb22ccbb22ccbb22ccbb22ccbb22ccbb22ccbb22ccbb22ccb"
      "b22ccbb22ccbb22ccbb22ccbb22ccbb22ccbb22ccbb22ccbb22ccbb26c951f05"
      "29080a";
  VerifyCompressedVector(kDynamicZlib, "dynamic Huffman");

  {
    const std::array entries = {TestEntry{u"../evil.txt", {1, 2, 3}}};
    const auto blob = BuildBlob(entries);
    TemporarySsf file = WriteSsf(MakeStoredZlib(blob), blob.size());
    const auto decoded = ziliu::settings::DecodeSogouSsfV3(file.path());
    Expect(!decoded.ok() && decoded.error.find("上级目录") != std::string::npos,
           "path traversal should be rejected");
  }

  {
    const std::array entries = {
        TestEntry{u"Assets/A.png", {1}},
        TestEntry{u"assets/a.png", {2}},
    };
    const auto blob = BuildBlob(entries);
    TemporarySsf file = WriteSsf(MakeStoredZlib(blob), blob.size());
    const auto decoded = ziliu::settings::DecodeSogouSsfV3(file.path());
    Expect(!decoded.ok() && decoded.error.find("大小写") != std::string::npos,
           "Windows case-colliding paths should be rejected");
  }

  {
    const std::array entries = {
        TestEntry{u"assets", {1}},
        TestEntry{u"Assets/a.png", {2}},
    };
    const auto blob = BuildBlob(entries);
    TemporarySsf file = WriteSsf(MakeStoredZlib(blob), blob.size());
    const auto decoded = ziliu::settings::DecodeSogouSsfV3(file.path());
    Expect(!decoded.ok() &&
               decoded.error.find("文件/目录") != std::string::npos,
           "Windows file-versus-directory path conflicts should be rejected");
  }

  {
    const std::array entries = {TestEntry{
        std::u16string(1, static_cast<char16_t>(0xD800U)), {1}}};
    const auto blob = BuildBlob(entries);
    TemporarySsf file = WriteSsf(MakeStoredZlib(blob), blob.size());
    const auto decoded = ziliu::settings::DecodeSogouSsfV3(file.path());
    Expect(!decoded.ok() && decoded.error.find("代理项") != std::string::npos,
           "unpaired UTF-16 surrogate should be rejected");
  }

  {
    const std::array entries = {TestEntry{u"valid.txt", {1}}};
    auto blob = BuildBlob(entries);
    SetLe32(blob, 8, 13);
    TemporarySsf file = WriteSsf(MakeStoredZlib(blob), blob.size());
    const auto decoded = ziliu::settings::DecodeSogouSsfV3(file.path());
    Expect(!decoded.ok() && decoded.error.find("第一条记录") != std::string::npos,
           "non-contiguous first record offset should be rejected");
  }

  {
    const std::array entries = {TestEntry{u"valid.txt", {1}}};
    const auto blob = BuildBlob(entries);
    auto zlib = MakeStoredZlib(blob);
    zlib.back() ^= 0x01U;
    TemporarySsf file = WriteSsf(zlib, blob.size());
    const auto decoded = ziliu::settings::DecodeSogouSsfV3(file.path());
    Expect(!decoded.ok() && decoded.error.find("Adler-32") != std::string::npos,
           "invalid Adler-32 should be rejected");
  }

  {
    const std::array entries = {TestEntry{u"valid.txt", {1}}};
    const auto blob = BuildBlob(entries);
    TemporarySsf file =
        WriteSsf(MakeStoredZlib(blob), blob.size(), 3, true);
    const auto decoded = ziliu::settings::DecodeSogouSsfV3(file.path());
    Expect(!decoded.ok() && decoded.error.find("PKCS#7") != std::string::npos,
           "invalid AES padding should be rejected");
  }

  {
    const std::array entries = {TestEntry{u"valid.txt", {1}}};
    const auto blob = BuildBlob(entries);
    TemporarySsf file = WriteSsf(MakeStoredZlib(blob), blob.size(), 2);
    const auto decoded = ziliu::settings::DecodeSogouSsfV3(file.path());
    Expect(!decoded.ok() && decoded.error.find("v3") != std::string::npos,
           "non-v3 SSF header should be rejected");
  }

  {
    const std::array entries = {TestEntry{u"valid.txt", {1, 2, 3}}};
    const auto blob = BuildBlob(entries);
    TemporarySsf file = WriteSsf(MakeStoredZlib(blob), blob.size());
    const auto first = ziliu::settings::Sha256SogouSsfFile(file.path());
    const auto second = ziliu::settings::Sha256SogouSsfFile(file.path());
    Expect(first.has_value() && first->size() == 64 && first == second,
           "complete-package SHA-256 identity should be stable");
  }

  std::cout << "ziliu_sogou_ssf_container_tests: OK\n";
  return EXIT_SUCCESS;
}
