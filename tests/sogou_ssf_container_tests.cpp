#include "../src/settings/sogou_ssf_container.h"

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

void SetLe32(std::vector<std::uint8_t>& bytes, std::size_t offset,
             std::uint32_t value) {
  Expect(offset + 4U <= bytes.size(), "test LE32 write should be in bounds");
  bytes[offset] = static_cast<std::uint8_t>(value);
  bytes[offset + 1U] = static_cast<std::uint8_t>(value >> 8U);
  bytes[offset + 2U] = static_cast<std::uint8_t>(value >> 16U);
  bytes[offset + 3U] = static_cast<std::uint8_t>(value >> 24U);
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
        ziliu::settings::DecodeSogouSsfV3(std::filesystem::path(arguments[2]));
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
    std::cout << "extracted entries: " << decoded.entries.size() << '\n';
    return EXIT_SUCCESS;
  }
  if (argument_count == 2) {
    const auto decoded =
        ziliu::settings::DecodeSogouSsfV3(std::filesystem::path(arguments[1]));
    if (!decoded.ok()) {
      std::cerr << "FAILED: real SSF: " << decoded.error << '\n';
      return EXIT_FAILURE;
    }
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
    const std::array entries = {
        TestEntry{u"assets/候选.png", {0x89, 0x50, 0x4E, 0x47}},
    };
    const auto blob = BuildBlob(entries);
    const auto zlib = MakeStoredZlib(blob);
    TemporarySsf file = WriteSsf(zlib, blob.size());
    const auto decoded = ziliu::settings::DecodeSogouSsfV3(file.path());
    if (!decoded.ok()) {
      std::cerr << "FAILED: stored DEFLATE vector: " << decoded.error << '\n';
      return EXIT_FAILURE;
    }
    Expect(decoded.entries.size() == 1,
           "stored DEFLATE SSF should contain one entry");
    Expect(decoded.entries.front().relative_path == "assets/候选.png",
           "UTF-16 filename should become UTF-8 with forward slashes");
    Expect(decoded.entries.front().bytes == entries.front().bytes,
           "stored DEFLATE entry bytes should round-trip");
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
