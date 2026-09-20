#include "sogou_ssf_container.h"

#if !defined(_WIN32)
#error "The Sogou SSF container decoder is Windows-only."
#endif

#include <Windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <limits>
#include <memory>
#include <new>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#pragma comment(lib, "bcrypt.lib")

namespace ziliu::settings {
namespace {

// Format reference only (MIT); this decoder is an independent implementation:
// https://github.com/frg2089/unSSF
constexpr std::size_t kHeaderBytes = 8;
constexpr std::size_t kAesBlockBytes = 16;
constexpr std::size_t kMaximumArchiveBytes = 64U * 1024U * 1024U;
constexpr std::size_t kMaximumInflatedBytes = 64U * 1024U * 1024U;
constexpr std::size_t kMaximumEntries = 128;
constexpr std::size_t kMaximumTotalContentBytes = 32U * 1024U * 1024U;
constexpr std::size_t kMaximumEntryContentBytes = 8U * 1024U * 1024U;
constexpr std::size_t kMaximumFilenameBytes = 32U * 1024U;
constexpr std::size_t kMaximumDeflateBlocks = 1024U * 1024U;
constexpr std::uint32_t kAdlerModulus = 65521;

constexpr std::array<std::uint8_t, 32> kAesKey = {
    0x52, 0x36, 0x46, 0x1A, 0xD3, 0x85, 0x03, 0x66,
    0x90, 0x45, 0x16, 0x28, 0x79, 0x03, 0x36, 0x23,
    0xDD, 0xBE, 0x6F, 0x03, 0xFF, 0x04, 0xE3, 0xCA,
    0xD5, 0x7F, 0xFC, 0xA3, 0x50, 0xE4, 0x9E, 0xD9,
};

constexpr std::array<std::uint8_t, 16> kAesIv = {
    0xE0, 0x7A, 0xAD, 0x35, 0xE0, 0x90, 0xAA, 0x03,
    0x8A, 0x51, 0xFD, 0x05, 0xDF, 0x8C, 0x5D, 0x0F,
};

constexpr std::array<std::uint16_t, 29> kLengthBases = {
    3,   4,   5,   6,   7,   8,   9,   10,  11,  13,
    15,  17,  19,  23,  27,  31,  35,  43,  51,  59,
    67,  83,  99,  115, 131, 163, 195, 227, 258,
};

constexpr std::array<std::uint8_t, 29> kLengthExtraBits = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
    2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0,
};

constexpr std::array<std::uint16_t, 30> kDistanceBases = {
    1,    2,    3,    4,    5,    7,    9,    13,   17,   25,
    33,   49,   65,   97,   129,  193,  257,  385,  513,  769,
    1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577,
};

constexpr std::array<std::uint8_t, 30> kDistanceExtraBits = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6,
    6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13,
};

[[nodiscard]] std::uint32_t ReadLe32(std::span<const std::uint8_t> bytes,
                                     std::size_t offset) noexcept {
  return static_cast<std::uint32_t>(bytes[offset]) |
         (static_cast<std::uint32_t>(bytes[offset + 1]) << 8U) |
         (static_cast<std::uint32_t>(bytes[offset + 2]) << 16U) |
         (static_cast<std::uint32_t>(bytes[offset + 3]) << 24U);
}

[[nodiscard]] std::uint32_t ReadBe32(std::span<const std::uint8_t> bytes,
                                     std::size_t offset) noexcept {
  return (static_cast<std::uint32_t>(bytes[offset]) << 24U) |
         (static_cast<std::uint32_t>(bytes[offset + 1]) << 16U) |
         (static_cast<std::uint32_t>(bytes[offset + 2]) << 8U) |
         static_cast<std::uint32_t>(bytes[offset + 3]);
}

[[nodiscard]] std::string WindowsErrorMessage(std::string_view prefix, DWORD error) {
  std::ostringstream stream;
  stream << prefix << "（Win32 错误 " << error << "）";
  return stream.str();
}

[[nodiscard]] std::string NtStatusMessage(std::string_view prefix, NTSTATUS status) {
  std::ostringstream stream;
  stream << prefix << "（NTSTATUS 0x" << std::hex << std::uppercase
         << static_cast<std::uint32_t>(status) << "）";
  return stream.str();
}

struct FileHandleCloser {
  void operator()(void* handle) const noexcept {
    if (handle != nullptr && handle != INVALID_HANDLE_VALUE) {
      CloseHandle(handle);
    }
  }
};

using UniqueFileHandle = std::unique_ptr<void, FileHandleCloser>;

struct AlgorithmHandle {
  BCRYPT_ALG_HANDLE value = nullptr;

  ~AlgorithmHandle() {
    if (value != nullptr) {
      BCryptCloseAlgorithmProvider(value, 0);
    }
  }

  AlgorithmHandle(const AlgorithmHandle&) = delete;
  AlgorithmHandle& operator=(const AlgorithmHandle&) = delete;
  AlgorithmHandle() = default;
};

struct KeyHandle {
  BCRYPT_KEY_HANDLE value = nullptr;

  ~KeyHandle() {
    if (value != nullptr) {
      BCryptDestroyKey(value);
    }
  }

  KeyHandle(const KeyHandle&) = delete;
  KeyHandle& operator=(const KeyHandle&) = delete;
  KeyHandle() = default;
};

[[nodiscard]] bool ReadArchive(const std::filesystem::path& source_path,
                               std::vector<std::uint8_t>& bytes,
                               std::string& error) {
  if (source_path.empty()) {
    error = "SSF 文件路径为空。";
    return false;
  }

  UniqueFileHandle file(CreateFileW(source_path.c_str(), GENERIC_READ,
                                    FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
                                    OPEN_EXISTING,
                                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
  if (file.get() == INVALID_HANDLE_VALUE) {
    error = WindowsErrorMessage("无法打开 SSF 文件", GetLastError());
    return false;
  }

  LARGE_INTEGER file_size{};
  if (!GetFileSizeEx(file.get(), &file_size)) {
    error = WindowsErrorMessage("无法读取 SSF 文件大小", GetLastError());
    return false;
  }
  if (file_size.QuadPart < static_cast<LONGLONG>(kHeaderBytes + kAesBlockBytes)) {
    error = "SSF 文件过短，缺少完整文件头或加密数据。";
    return false;
  }
  if (file_size.QuadPart > static_cast<LONGLONG>(kMaximumArchiveBytes)) {
    error = "SSF 文件超过 64 MiB 安全上限。";
    return false;
  }

  const auto size = static_cast<std::size_t>(file_size.QuadPart);
  bytes.resize(size);
  std::size_t completed = 0;
  while (completed < bytes.size()) {
    const std::size_t remaining = bytes.size() - completed;
    const DWORD chunk = static_cast<DWORD>((std::min)(
        remaining, static_cast<std::size_t>((std::numeric_limits<DWORD>::max)())));
    DWORD read = 0;
    if (!ReadFile(file.get(), bytes.data() + completed, chunk, &read, nullptr)) {
      error = WindowsErrorMessage("读取 SSF 文件失败", GetLastError());
      return false;
    }
    if (read == 0) {
      error = "读取 SSF 文件时意外到达文件末尾。";
      return false;
    }
    completed += read;
  }
  return true;
}

[[nodiscard]] bool DecryptPayload(std::span<const std::uint8_t> encrypted,
                                  std::vector<std::uint8_t>& plaintext,
                                  std::string& error) {
  if (encrypted.empty() || encrypted.size() % kAesBlockBytes != 0) {
    error = "SSF 加密区长度不是有效的 AES-CBC 块长度。";
    return false;
  }
  if (encrypted.size() > (std::numeric_limits<ULONG>::max)()) {
    error = "SSF 加密区过大，无法交给 Windows CNG。";
    return false;
  }

  AlgorithmHandle algorithm;
  NTSTATUS status =
      BCryptOpenAlgorithmProvider(&algorithm.value, BCRYPT_AES_ALGORITHM, nullptr, 0);
  if (!BCRYPT_SUCCESS(status)) {
    error = NtStatusMessage("无法初始化 Windows AES 解码器", status);
    return false;
  }

  status = BCryptSetProperty(
      algorithm.value, BCRYPT_CHAINING_MODE,
      reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_CBC)),
      static_cast<ULONG>(sizeof(BCRYPT_CHAIN_MODE_CBC)), 0);
  if (!BCRYPT_SUCCESS(status)) {
    error = NtStatusMessage("无法设置 AES-CBC 模式", status);
    return false;
  }

  ULONG key_object_bytes = 0;
  ULONG returned_bytes = 0;
  status = BCryptGetProperty(algorithm.value, BCRYPT_OBJECT_LENGTH,
                             reinterpret_cast<PUCHAR>(&key_object_bytes),
                             sizeof(key_object_bytes), &returned_bytes, 0);
  if (!BCRYPT_SUCCESS(status) || returned_bytes != sizeof(key_object_bytes) ||
      key_object_bytes == 0) {
    error = NtStatusMessage("无法查询 AES 密钥对象大小", status);
    return false;
  }

  std::vector<std::uint8_t> key_object(key_object_bytes);
  KeyHandle key;
  status = BCryptGenerateSymmetricKey(
      algorithm.value, &key.value, key_object.data(), key_object_bytes,
      const_cast<PUCHAR>(kAesKey.data()), static_cast<ULONG>(kAesKey.size()), 0);
  if (!BCRYPT_SUCCESS(status)) {
    error = NtStatusMessage("无法导入 SSF AES 密钥", status);
    return false;
  }

  const ULONG encrypted_bytes = static_cast<ULONG>(encrypted.size());
  ULONG required_bytes = 0;
  auto query_iv = kAesIv;
  status = BCryptDecrypt(key.value, const_cast<PUCHAR>(encrypted.data()),
                         encrypted_bytes, nullptr, query_iv.data(),
                         static_cast<ULONG>(query_iv.size()), nullptr, 0,
                         &required_bytes, BCRYPT_BLOCK_PADDING);
  if (!BCRYPT_SUCCESS(status) || required_bytes == 0 ||
      required_bytes > encrypted_bytes) {
    error = NtStatusMessage("SSF AES 解密长度校验失败", status);
    return false;
  }

  plaintext.resize(required_bytes);
  ULONG plaintext_bytes = 0;
  auto decrypt_iv = kAesIv;
  status = BCryptDecrypt(key.value, const_cast<PUCHAR>(encrypted.data()),
                         encrypted_bytes, nullptr, decrypt_iv.data(),
                         static_cast<ULONG>(decrypt_iv.size()), plaintext.data(),
                         static_cast<ULONG>(plaintext.size()), &plaintext_bytes,
                         BCRYPT_BLOCK_PADDING);
  if (!BCRYPT_SUCCESS(status) || plaintext_bytes == 0 ||
      plaintext_bytes > plaintext.size()) {
    error = NtStatusMessage("SSF AES-256-CBC 解密或 PKCS#7 填充校验失败", status);
    return false;
  }
  plaintext.resize(plaintext_bytes);
  return true;
}

class BitReader {
 public:
  explicit BitReader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

  [[nodiscard]] bool ReadBits(unsigned bit_count, std::uint32_t& value) noexcept {
    if (bit_count > 24U || bit_position_ > bytes_.size() * 8U ||
        bit_count > bytes_.size() * 8U - bit_position_) {
      return false;
    }
    value = 0;
    for (unsigned index = 0; index < bit_count; ++index) {
      const std::size_t absolute_bit = bit_position_++;
      const std::uint8_t byte = bytes_[absolute_bit / 8U];
      const unsigned bit = (byte >> (absolute_bit % 8U)) & 1U;
      value |= static_cast<std::uint32_t>(bit) << index;
    }
    return true;
  }

  [[nodiscard]] bool AlignToByte() noexcept {
    if (bit_position_ > (std::numeric_limits<std::size_t>::max)() - 7U) {
      return false;
    }
    bit_position_ = (bit_position_ + 7U) & ~std::size_t{7};
    return bit_position_ <= bytes_.size() * 8U;
  }

  [[nodiscard]] std::size_t consumed_bytes() const noexcept {
    return (bit_position_ + 7U) / 8U;
  }

 private:
  std::span<const std::uint8_t> bytes_;
  std::size_t bit_position_ = 0;
};

class HuffmanTree {
 public:
  [[nodiscard]] bool Build(std::span<const std::uint8_t> lengths,
                           bool allow_single_symbol,
                           bool allow_empty,
                           std::string_view description,
                           std::string& error) {
    nodes_.clear();
    nodes_.push_back(Node{});

    std::array<std::uint16_t, 16> counts{};
    unsigned maximum_length = 0;
    std::size_t symbol_count = 0;
    for (const std::uint8_t length : lengths) {
      if (length > 15) {
        error = std::string(description) + "码长超过 DEFLATE 的 15 位上限。";
        return false;
      }
      if (length != 0) {
        ++counts[length];
        maximum_length = (std::max)(maximum_length, static_cast<unsigned>(length));
        ++symbol_count;
      }
    }
    if (symbol_count == 0) {
      if (allow_empty) {
        maximum_length_ = 0;
        return true;
      }
      error = std::string(description) + "为空。";
      return false;
    }

    int slots = 1;
    for (unsigned bits = 1; bits <= 15; ++bits) {
      slots = slots * 2 - counts[bits];
      if (slots < 0) {
        error = std::string(description) + "存在过度订阅的 Huffman 码。";
        return false;
      }
    }
    if (slots != 0 &&
        !(allow_single_symbol && symbol_count == 1 && maximum_length == 1)) {
      error = std::string(description) + "是不完整的 Huffman 码。";
      return false;
    }

    std::array<std::uint16_t, 16> next_codes{};
    std::uint16_t code = 0;
    for (unsigned bits = 1; bits <= 15; ++bits) {
      code = static_cast<std::uint16_t>(
          (code + counts[bits - 1U]) << 1U);
      next_codes[bits] = code;
    }

    maximum_length_ = maximum_length;
    for (std::size_t symbol = 0; symbol < lengths.size(); ++symbol) {
      const unsigned length = lengths[symbol];
      if (length == 0) {
        continue;
      }
      const std::uint16_t symbol_code = next_codes[length]++;
      std::size_t node_index = 0;
      for (unsigned depth = 0; depth < length; ++depth) {
        if (nodes_[node_index].symbol >= 0) {
          error = std::string(description) + "存在前缀冲突。";
          return false;
        }
        const unsigned shift = length - depth - 1U;
        const unsigned branch = (symbol_code >> shift) & 1U;
        int child = nodes_[node_index].children[branch];
        if (child < 0) {
          child = static_cast<int>(nodes_.size());
          nodes_[node_index].children[branch] = child;
          nodes_.push_back(Node{});
        }
        node_index = static_cast<std::size_t>(child);
      }
      Node& leaf = nodes_[node_index];
      if (leaf.symbol >= 0 || leaf.children[0] >= 0 || leaf.children[1] >= 0) {
        error = std::string(description) + "存在重复或前缀冲突。";
        return false;
      }
      if (symbol > static_cast<std::size_t>((std::numeric_limits<int>::max)())) {
        error = std::string(description) + "符号索引过大。";
        return false;
      }
      leaf.symbol = static_cast<int>(symbol);
    }
    return true;
  }

  [[nodiscard]] bool Decode(BitReader& reader, std::uint16_t& symbol,
                            std::string_view description,
                            std::string& error) const {
    std::size_t node_index = 0;
    for (unsigned depth = 0; depth < maximum_length_; ++depth) {
      std::uint32_t branch = 0;
      if (!reader.ReadBits(1, branch)) {
        error = std::string(description) + "在 Huffman 符号中意外结束。";
        return false;
      }
      const int child = nodes_[node_index].children[branch];
      if (child < 0) {
        error = std::string(description) + "包含不存在的 Huffman 码。";
        return false;
      }
      node_index = static_cast<std::size_t>(child);
      if (nodes_[node_index].symbol >= 0) {
        symbol = static_cast<std::uint16_t>(nodes_[node_index].symbol);
        return true;
      }
    }
    error = std::string(description) + "包含超过最大码长的 Huffman 码。";
    return false;
  }

 private:
  struct Node {
    std::array<int, 2> children{-1, -1};
    int symbol = -1;
  };

  std::vector<Node> nodes_;
  unsigned maximum_length_ = 0;
};

[[nodiscard]] bool BuildFixedTrees(HuffmanTree& literal_length_tree,
                                   HuffmanTree& distance_tree,
                                   std::string& error) {
  std::array<std::uint8_t, 288> literal_lengths{};
  std::fill(literal_lengths.begin(), literal_lengths.begin() + 144,
            std::uint8_t{8});
  std::fill(literal_lengths.begin() + 144, literal_lengths.begin() + 256,
            std::uint8_t{9});
  std::fill(literal_lengths.begin() + 256, literal_lengths.begin() + 280,
            std::uint8_t{7});
  std::fill(literal_lengths.begin() + 280, literal_lengths.end(),
            std::uint8_t{8});
  if (!literal_length_tree.Build(literal_lengths, false, false,
                                 "固定字面值/长度树", error)) {
    return false;
  }

  std::array<std::uint8_t, 32> distance_lengths{};
  distance_lengths.fill(5);
  return distance_tree.Build(distance_lengths, false, false, "固定距离树",
                             error);
}

[[nodiscard]] bool BuildDynamicTrees(BitReader& reader,
                                     HuffmanTree& literal_length_tree,
                                     HuffmanTree& distance_tree,
                                     std::string& error) {
  std::uint32_t value = 0;
  if (!reader.ReadBits(5, value)) {
    error = "动态 DEFLATE 块缺少 HLIT。";
    return false;
  }
  const std::size_t literal_count = value + 257U;
  if (literal_count > 286U) {
    error = "动态 DEFLATE 块的 HLIT 使用了保留值。";
    return false;
  }
  if (!reader.ReadBits(5, value)) {
    error = "动态 DEFLATE 块缺少 HDIST。";
    return false;
  }
  const std::size_t distance_count = value + 1U;
  if (!reader.ReadBits(4, value)) {
    error = "动态 DEFLATE 块缺少 HCLEN。";
    return false;
  }
  const std::size_t code_length_count = value + 4U;

  constexpr std::array<std::uint8_t, 19> kCodeLengthOrder = {
      16, 17, 18, 0, 8, 7, 9, 6, 10, 5,
      11, 4, 12, 3, 13, 2, 14, 1, 15,
  };
  std::array<std::uint8_t, 19> code_lengths{};
  for (std::size_t index = 0; index < code_length_count; ++index) {
    if (!reader.ReadBits(3, value)) {
      error = "动态 DEFLATE 块的码长树被截断。";
      return false;
    }
    code_lengths[kCodeLengthOrder[index]] = static_cast<std::uint8_t>(value);
  }

  HuffmanTree code_length_tree;
  if (!code_length_tree.Build(code_lengths, false, false, "动态码长树",
                              error)) {
    return false;
  }

  const std::size_t combined_count = literal_count + distance_count;
  std::vector<std::uint8_t> combined_lengths;
  combined_lengths.reserve(combined_count);
  while (combined_lengths.size() < combined_count) {
    std::uint16_t symbol = 0;
    if (!code_length_tree.Decode(reader, symbol, "动态码长树", error)) {
      return false;
    }
    if (symbol <= 15) {
      combined_lengths.push_back(static_cast<std::uint8_t>(symbol));
      continue;
    }

    std::size_t repeat = 0;
    std::uint8_t repeated_length = 0;
    if (symbol == 16) {
      if (combined_lengths.empty()) {
        error = "动态码长重复符号 16 没有前一个码长。";
        return false;
      }
      if (!reader.ReadBits(2, value)) {
        error = "动态码长重复符号 16 被截断。";
        return false;
      }
      repeat = value + 3U;
      repeated_length = combined_lengths.back();
    } else if (symbol == 17) {
      if (!reader.ReadBits(3, value)) {
        error = "动态码长重复符号 17 被截断。";
        return false;
      }
      repeat = value + 3U;
    } else if (symbol == 18) {
      if (!reader.ReadBits(7, value)) {
        error = "动态码长重复符号 18 被截断。";
        return false;
      }
      repeat = value + 11U;
    } else {
      error = "动态码长树解出了保留符号。";
      return false;
    }
    if (repeat > combined_count - combined_lengths.size()) {
      error = "动态码长重复超出了 HLIT/HDIST 声明范围。";
      return false;
    }
    combined_lengths.insert(combined_lengths.end(), repeat, repeated_length);
  }

  const std::span<const std::uint8_t> literal_lengths(combined_lengths.data(),
                                                       literal_count);
  const std::span<const std::uint8_t> distance_lengths(
      combined_lengths.data() + literal_count, distance_count);
  if (literal_lengths[256] == 0) {
    error = "动态字面值/长度树缺少块结束符号 256。";
    return false;
  }
  if (!literal_length_tree.Build(literal_lengths, true, false,
                                 "动态字面值/长度树", error)) {
    return false;
  }
  // RFC 1951 permits a literal-only dynamic block to declare one distance
  // code with length zero. Decode() still rejects that empty tree if a match
  // unexpectedly tries to use it.
  return distance_tree.Build(distance_lengths, true, true, "动态距离树",
                             error);
}

[[nodiscard]] bool InflateCompressedBlock(BitReader& reader,
                                          const HuffmanTree& literal_length_tree,
                                          const HuffmanTree& distance_tree,
                                          std::size_t expected_size,
                                          std::vector<std::uint8_t>& output,
                                          std::string& error) {
  while (true) {
    std::uint16_t symbol = 0;
    if (!literal_length_tree.Decode(reader, symbol, "字面值/长度树", error)) {
      return false;
    }
    if (symbol < 256) {
      if (output.size() >= expected_size) {
        error = "DEFLATE 输出超过声明的精确长度。";
        return false;
      }
      output.push_back(static_cast<std::uint8_t>(symbol));
      continue;
    }
    if (symbol == 256) {
      return true;
    }
    if (symbol < 257 || symbol > 285) {
      error = "DEFLATE 字面值/长度树解出了保留符号。";
      return false;
    }

    const std::size_t length_index = symbol - 257U;
    std::uint32_t extra = 0;
    if (!reader.ReadBits(kLengthExtraBits[length_index], extra)) {
      error = "DEFLATE 长度附加位被截断。";
      return false;
    }
    const std::size_t length = kLengthBases[length_index] + extra;

    std::uint16_t distance_symbol = 0;
    if (!distance_tree.Decode(reader, distance_symbol, "距离树", error)) {
      return false;
    }
    if (distance_symbol >= kDistanceBases.size()) {
      error = "DEFLATE 距离树解出了保留符号。";
      return false;
    }
    if (!reader.ReadBits(kDistanceExtraBits[distance_symbol], extra)) {
      error = "DEFLATE 距离附加位被截断。";
      return false;
    }
    const std::size_t distance = kDistanceBases[distance_symbol] + extra;
    if (distance == 0 || distance > output.size()) {
      error = "DEFLATE 回溯距离越过了已产生的输出。";
      return false;
    }
    if (length > expected_size - output.size()) {
      std::ostringstream stream;
      stream << "DEFLATE 复制长度超过声明的精确输出长度（已输出 "
             << output.size() << "，复制 " << length << "，声明 "
             << expected_size << "）。";
      error = stream.str();
      return false;
    }
    for (std::size_t index = 0; index < length; ++index) {
      output.push_back(output[output.size() - distance]);
    }
  }
}

[[nodiscard]] bool InflateDeflate(std::span<const std::uint8_t> deflate,
                                  std::size_t expected_size,
                                  std::vector<std::uint8_t>& output,
                                  std::string& error) {
  BitReader reader(deflate);
  bool final_block = false;
  std::size_t block_count = 0;
  while (!final_block) {
    if (++block_count > kMaximumDeflateBlocks) {
      error = "DEFLATE 块数量超过安全上限。";
      return false;
    }
    std::uint32_t value = 0;
    if (!reader.ReadBits(1, value)) {
      error = "DEFLATE 数据缺少最终块标志。";
      return false;
    }
    final_block = value != 0;
    if (!reader.ReadBits(2, value)) {
      error = "DEFLATE 数据缺少块类型。";
      return false;
    }

    if (value == 0) {
      if (!reader.AlignToByte()) {
        error = "DEFLATE 存储块的字节对齐无效。";
        return false;
      }
      std::uint32_t length = 0;
      std::uint32_t complement = 0;
      if (!reader.ReadBits(16, length) || !reader.ReadBits(16, complement)) {
        error = "DEFLATE 存储块头被截断。";
        return false;
      }
      if ((length ^ 0xFFFFU) != complement) {
        error = "DEFLATE 存储块的 LEN/NLEN 校验失败。";
        return false;
      }
      if (length > expected_size - output.size()) {
        error = "DEFLATE 存储块超过声明的精确输出长度。";
        return false;
      }
      for (std::uint32_t index = 0; index < length; ++index) {
        if (!reader.ReadBits(8, value)) {
          error = "DEFLATE 存储块内容被截断。";
          return false;
        }
        output.push_back(static_cast<std::uint8_t>(value));
      }
      continue;
    }

    HuffmanTree literal_length_tree;
    HuffmanTree distance_tree;
    if (value == 1) {
      if (!BuildFixedTrees(literal_length_tree, distance_tree, error)) {
        return false;
      }
    } else if (value == 2) {
      if (!BuildDynamicTrees(reader, literal_length_tree, distance_tree, error)) {
        return false;
      }
    } else {
      error = "DEFLATE 使用了保留块类型 3。";
      return false;
    }
    if (!InflateCompressedBlock(reader, literal_length_tree, distance_tree,
                                expected_size, output, error)) {
      return false;
    }
  }

  if (reader.consumed_bytes() != deflate.size()) {
    error = "DEFLATE 最终块之后仍有额外压缩数据。";
    return false;
  }
  if (output.size() != expected_size) {
    error = "DEFLATE 实际输出长度与 SSF 声明不一致。";
    return false;
  }
  return true;
}

[[nodiscard]] std::uint32_t ComputeAdler32(
    std::span<const std::uint8_t> bytes) noexcept {
  std::uint32_t first = 1;
  std::uint32_t second = 0;
  std::size_t offset = 0;
  while (offset < bytes.size()) {
    const std::size_t chunk_size = (std::min)(bytes.size() - offset,
                                               std::size_t{5552});
    const std::size_t end = offset + chunk_size;
    for (; offset < end; ++offset) {
      first += bytes[offset];
      second += first;
    }
    first %= kAdlerModulus;
    second %= kAdlerModulus;
  }
  return (second << 16U) | first;
}

[[nodiscard]] bool InflateZlib(std::span<const std::uint8_t> zlib,
                               std::size_t expected_size,
                               std::vector<std::uint8_t>& output,
                               std::string& error) {
  if (zlib.size() < 7) {
    error = "zlib 数据过短。";
    return false;
  }
  const std::uint8_t cmf = zlib[0];
  const std::uint8_t flags = zlib[1];
  if ((cmf & 0x0FU) != 8U || (cmf >> 4U) > 7U) {
    error = "zlib 头不是受支持的 DEFLATE/32 KiB 窗口。";
    return false;
  }
  if ((static_cast<unsigned>(cmf) * 256U + flags) % 31U != 0) {
    error = "zlib FCHECK 校验失败。";
    return false;
  }
  if ((flags & 0x20U) != 0) {
    error = "zlib 预置字典不受 SSF 解码器支持。";
    return false;
  }

  const std::size_t checksum_offset = zlib.size() - 4U;
  const auto deflate = zlib.subspan(2, checksum_offset - 2U);
  output.clear();
  output.reserve(expected_size);
  if (!InflateDeflate(deflate, expected_size, output, error)) {
    return false;
  }
  const std::uint32_t expected_adler = ReadBe32(zlib, checksum_offset);
  const std::uint32_t actual_adler = ComputeAdler32(output);
  if (actual_adler != expected_adler) {
    error = "zlib Adler-32 校验失败。";
    return false;
  }
  return true;
}

[[nodiscard]] bool IsHighSurrogate(std::uint16_t value) noexcept {
  return value >= 0xD800U && value <= 0xDBFFU;
}

[[nodiscard]] bool IsLowSurrogate(std::uint16_t value) noexcept {
  return value >= 0xDC00U && value <= 0xDFFFU;
}

[[nodiscard]] bool IsReservedDeviceName(std::wstring_view component) {
  const std::size_t dot = component.find(L'.');
  const std::wstring_view base = component.substr(0, dot);
  std::wstring uppercase;
  uppercase.reserve(base.size());
  for (const wchar_t character : base) {
    if (character >= L'a' && character <= L'z') {
      uppercase.push_back(static_cast<wchar_t>(character - L'a' + L'A'));
    } else {
      uppercase.push_back(character);
    }
  }
  if (uppercase == L"CON" || uppercase == L"PRN" || uppercase == L"AUX" ||
      uppercase == L"NUL" || uppercase == L"CONIN$" ||
      uppercase == L"CONOUT$") {
    return true;
  }
  if (uppercase.size() == 4 &&
      (uppercase.starts_with(L"COM") || uppercase.starts_with(L"LPT"))) {
    const wchar_t suffix = uppercase[3];
    if ((suffix >= L'1' && suffix <= L'9') || suffix == L'\u00B9' ||
        suffix == L'\u00B2' || suffix == L'\u00B3') {
      return true;
    }
  }
  return false;
}

[[nodiscard]] bool ValidateWindowsRelativePath(std::wstring_view path,
                                               std::string& error) {
  if (path.empty()) {
    error = "SSF 条目文件名为空。";
    return false;
  }
  if (path.front() == L'/' || path.back() == L'/') {
    error = "SSF 条目路径不能是绝对路径或以目录分隔符结尾。";
    return false;
  }

  std::size_t component_start = 0;
  for (std::size_t index = 0; index <= path.size(); ++index) {
    if (index != path.size() && path[index] != L'/') {
      const wchar_t character = path[index];
      if (character == L'\\' || character == L':' || character == L'<' ||
          character == L'>' || character == L'"' || character == L'|' ||
          character == L'?' || character == L'*' || character == L'\0' ||
          character < L' ') {
        error = "SSF 条目路径包含 Windows 不安全字符。";
        return false;
      }
      continue;
    }

    const std::wstring_view component =
        path.substr(component_start, index - component_start);
    if (component.empty() || component == L"." || component == L"..") {
      error = "SSF 条目路径包含空目录、当前目录或上级目录。";
      return false;
    }
    if (component.size() > 255) {
      error = "SSF 条目路径组件超过 255 个 UTF-16 代码单元。";
      return false;
    }
    if (component.back() == L'.' || component.back() == L' ') {
      error = "SSF 条目路径组件不能以点或空格结尾。";
      return false;
    }
    if (IsReservedDeviceName(component)) {
      error = "SSF 条目路径使用了 Windows 保留设备名。";
      return false;
    }
    component_start = index + 1U;
  }
  return true;
}

[[nodiscard]] bool DecodeStrictUtf16(std::span<const std::uint8_t> bytes,
                                     std::wstring& wide_path,
                                     std::string& utf8_path,
                                     std::string& error) {
  if (bytes.empty() || bytes.size() % 2U != 0 ||
      bytes.size() > kMaximumFilenameBytes) {
    error = "SSF 条目文件名长度无效。";
    return false;
  }
  const std::size_t code_unit_count = bytes.size() / 2U;
  if (code_unit_count >
      static_cast<std::size_t>((std::numeric_limits<int>::max)())) {
    error = "SSF 条目文件名过长，无法转换为 UTF-8。";
    return false;
  }

  wide_path.clear();
  wide_path.reserve(code_unit_count);
  for (std::size_t index = 0; index < code_unit_count; ++index) {
    const std::uint16_t code_unit =
        static_cast<std::uint16_t>(bytes[index * 2U]) |
        (static_cast<std::uint16_t>(bytes[index * 2U + 1U]) << 8U);
    if (IsHighSurrogate(code_unit)) {
      if (index + 1U >= code_unit_count) {
        error = "SSF 条目文件名含有未配对的 UTF-16 高代理项。";
        return false;
      }
      const std::uint16_t next =
          static_cast<std::uint16_t>(bytes[(index + 1U) * 2U]) |
          (static_cast<std::uint16_t>(bytes[(index + 1U) * 2U + 1U]) << 8U);
      if (!IsLowSurrogate(next)) {
        error = "SSF 条目文件名含有未配对的 UTF-16 高代理项。";
        return false;
      }
      wide_path.push_back(static_cast<wchar_t>(code_unit));
      wide_path.push_back(static_cast<wchar_t>(next));
      ++index;
      continue;
    }
    if (IsLowSurrogate(code_unit)) {
      error = "SSF 条目文件名含有未配对的 UTF-16 低代理项。";
      return false;
    }
    wide_path.push_back(static_cast<wchar_t>(code_unit));
  }

  if (!ValidateWindowsRelativePath(wide_path, error)) {
    return false;
  }
  const int wide_length = static_cast<int>(wide_path.size());
  const int utf8_length =
      WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide_path.data(),
                          wide_length, nullptr, 0, nullptr, nullptr);
  if (utf8_length <= 0) {
    error = WindowsErrorMessage("SSF 条目文件名无法严格转换为 UTF-8",
                                GetLastError());
    return false;
  }
  utf8_path.resize(static_cast<std::size_t>(utf8_length));
  const int converted =
      WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide_path.data(),
                          wide_length, utf8_path.data(), utf8_length, nullptr,
                          nullptr);
  if (converted != utf8_length) {
    error = WindowsErrorMessage("SSF 条目文件名 UTF-8 转换不完整",
                                GetLastError());
    return false;
  }
  return true;
}

[[nodiscard]] bool PathsCollide(std::wstring_view left,
                                std::wstring_view right) noexcept {
  if (left.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()) ||
      right.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)())) {
    return false;
  }
  return CompareStringOrdinal(left.data(), static_cast<int>(left.size()),
                              right.data(), static_cast<int>(right.size()),
                              TRUE) == CSTR_EQUAL;
}

[[nodiscard]] bool PathPrefixes(std::wstring_view prefix,
                                std::wstring_view path) noexcept {
  if (prefix.size() >= path.size() || path[prefix.size()] != L'/' ||
      prefix.size() >
          static_cast<std::size_t>((std::numeric_limits<int>::max)())) {
    return false;
  }
  return CompareStringOrdinal(
             prefix.data(), static_cast<int>(prefix.size()), path.data(),
             static_cast<int>(prefix.size()), TRUE) == CSTR_EQUAL;
}

[[nodiscard]] bool PathsConflict(std::wstring_view left,
                                 std::wstring_view right) noexcept {
  return PathsCollide(left, right) || PathPrefixes(left, right) ||
         PathPrefixes(right, left);
}

[[nodiscard]] bool ParseOutputBlob(std::span<const std::uint8_t> blob,
                                   std::vector<SogouSsfEntry>& entries,
                                   std::string& error) {
  if (blob.size() < 8) {
    error = "SSF 解压结果过短，缺少总长度和偏移表长度。";
    return false;
  }
  if (ReadLe32(blob, 0) != blob.size()) {
    error = "SSF 解压结果的总长度字段不精确。";
    return false;
  }

  const std::size_t offset_table_bytes = ReadLe32(blob, 4);
  if (offset_table_bytes % 4U != 0) {
    error = "SSF 偏移表长度不是 4 的倍数。";
    return false;
  }
  const std::size_t entry_count = offset_table_bytes / 4U;
  if (entry_count > kMaximumEntries) {
    error = "SSF 条目数量超过 128 个安全上限。";
    return false;
  }
  if (offset_table_bytes > blob.size() - 8U) {
    error = "SSF 偏移表越过了解压结果边界。";
    return false;
  }

  const std::size_t records_start = 8U + offset_table_bytes;
  std::vector<std::size_t> offsets;
  offsets.reserve(entry_count);
  for (std::size_t index = 0; index < entry_count; ++index) {
    const std::size_t offset = ReadLe32(blob, 8U + index * 4U);
    if (offset < records_start || offset > blob.size()) {
      error = "SSF 条目偏移越过了记录区边界。";
      return false;
    }
    if (index == 0 && offset != records_start) {
      error = "SSF 第一条记录没有紧接偏移表。";
      return false;
    }
    if (index > 0 && offset <= offsets.back()) {
      error = "SSF 条目偏移没有严格单调递增。";
      return false;
    }
    offsets.push_back(offset);
  }
  if (entry_count == 0 && records_start != blob.size()) {
    error = "空 SSF 偏移表之后仍有额外数据。";
    return false;
  }

  entries.clear();
  entries.reserve(entry_count);
  std::vector<std::wstring> wide_paths;
  wide_paths.reserve(entry_count);
  std::size_t cursor = records_start;
  std::size_t total_content_bytes = 0;
  for (std::size_t index = 0; index < entry_count; ++index) {
    if (offsets[index] != cursor) {
      error = "SSF 条目记录之间存在空洞或重叠。";
      return false;
    }
    if (blob.size() - cursor < 4U) {
      error = "SSF 条目缺少文件名长度字段。";
      return false;
    }
    const std::size_t filename_bytes = ReadLe32(blob, cursor);
    cursor += 4U;
    if (filename_bytes > blob.size() - cursor) {
      error = "SSF 条目文件名越过了解压结果边界。";
      return false;
    }

    std::wstring wide_path;
    std::string utf8_path;
    if (!DecodeStrictUtf16(blob.subspan(cursor, filename_bytes), wide_path,
                           utf8_path, error)) {
      return false;
    }
    for (const std::wstring& existing : wide_paths) {
      if (PathsConflict(existing, wide_path)) {
        error =
            "SSF 条目路径在 Windows 大小写或文件/目录规则下发生冲突。";
        return false;
      }
    }
    cursor += filename_bytes;
    if (blob.size() - cursor < 4U) {
      error = "SSF 条目缺少内容长度字段。";
      return false;
    }
    const std::size_t content_bytes = ReadLe32(blob, cursor);
    cursor += 4U;
    if (content_bytes > kMaximumEntryContentBytes) {
      error = "SSF 单个条目超过 8 MiB 安全上限。";
      return false;
    }
    if (content_bytes > blob.size() - cursor) {
      error = "SSF 条目内容越过了解压结果边界。";
      return false;
    }
    if (content_bytes > kMaximumTotalContentBytes - total_content_bytes) {
      error = "SSF 条目内容总量超过 32 MiB 安全上限。";
      return false;
    }
    total_content_bytes += content_bytes;

    SogouSsfEntry entry;
    entry.relative_path = std::move(utf8_path);
    entry.bytes.assign(blob.begin() + static_cast<std::ptrdiff_t>(cursor),
                       blob.begin() +
                           static_cast<std::ptrdiff_t>(cursor + content_bytes));
    entries.push_back(std::move(entry));
    wide_paths.push_back(std::move(wide_path));
    cursor += content_bytes;
    if (index + 1U < entry_count && offsets[index + 1U] != cursor) {
      error = "SSF 条目记录长度与下一个偏移不连续。";
      return false;
    }
  }
  if (cursor != blob.size()) {
    error = "SSF 最后一条记录之后仍有额外数据。";
    return false;
  }
  return true;
}

}  // namespace

static std::uint16_t ZipU16(std::span<const std::uint8_t> b, std::size_t p) {
  return static_cast<std::uint16_t>(b[p] | (static_cast<unsigned>(b[p + 1]) << 8U));
}

static std::uint32_t ZipCrc(std::span<const std::uint8_t> bytes) {
  static const auto table = [] {
    std::array<std::uint32_t, 256> t{};
    for (std::uint32_t i = 0; i < 256; ++i) {
      auto c = i;
      for (int bit = 0; bit < 8; ++bit) c = (c >> 1U) ^ ((c & 1U) ? 0xedb88320U : 0U);
      t[i] = c;
    }
    return t;
  }();
  std::uint32_t crc = 0xffffffffU;
  for (const auto b : bytes) crc = table[(crc ^ b) & 255U] ^ (crc >> 8U);
  return crc ^ 0xffffffffU;
}

static bool DecodeZip(std::span<const std::uint8_t> b,
                      std::vector<SogouSsfEntry>& entries, std::string& error) {
  const auto fail = [&error](const char* message) { error = message; return false; };
  if (b.size() < 22) return fail("SSF ZIP header is truncated.");
  std::size_t end = b.size() - 22;
  const std::size_t first = b.size() > 65557 ? b.size() - 65557 : 0;
  for (;;) {
    if (ReadLe32(b, end) == 0x06054b50U && end + 22U + ZipU16(b, end + 20) == b.size()) break;
    if (end == first) return fail("SSF ZIP central directory is missing.");
    --end;
  }
  const auto count = ZipU16(b, end + 10);
  const std::size_t central = ReadLe32(b, end + 16);
  if (ZipU16(b, end + 4) || ZipU16(b, end + 6) || ZipU16(b, end + 8) != count ||
      count == 0 || count > kMaximumEntries || central > end ||
      ReadLe32(b, end + 12) != end - central) return fail("Unsupported SSF ZIP directory.");
  std::size_t cursor = central, total = 0;
  std::vector<std::pair<std::wstring, bool>> paths;
  std::vector<std::pair<std::size_t, std::size_t>> ranges;
  for (unsigned i = 0; i < count; ++i) {
    if (cursor > end || end - cursor < 46 || ReadLe32(b, cursor) != 0x02014b50U)
      return fail("Truncated SSF ZIP entry.");
    const auto flags = ZipU16(b, cursor + 8), method = ZipU16(b, cursor + 10);
    const auto crc = ReadLe32(b, cursor + 16);
    const std::size_t compressed = ReadLe32(b, cursor + 20), size = ReadLe32(b, cursor + 24);
    const std::size_t name_size = ZipU16(b, cursor + 28), extra = ZipU16(b, cursor + 30), comment = ZipU16(b, cursor + 32);
    const std::size_t local = ReadLe32(b, cursor + 42);
    const auto attributes = ReadLe32(b, cursor + 38);
    if (ZipU16(b, cursor + 6) > 20 || ZipU16(b, cursor + 34) ||
        (flags & ~0x080eU) || (method != 0 && method != 8) ||
        (method == 0 && ((flags & 6U) || compressed != size)) || !name_size ||
        name_size + extra + comment > end - cursor - 46 ||
        size > kMaximumEntryContentBytes || total > kMaximumTotalContentBytes - size)
      return fail("Unsupported or oversized SSF ZIP entry.");
    total += size;
    std::string raw(reinterpret_cast<const char*>(b.data() + cursor + 46), name_size);
    if (!(flags & 0x0800U) && std::any_of(raw.begin(), raw.end(), [](unsigned char c) { return c >= 128; }))
      return fail("SSF ZIP non-ASCII names must declare UTF-8.");
    const bool directory = raw.back() == '/';
    const std::string name = directory ? raw.substr(0, raw.size() - 1) : raw;
    const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name.data(), static_cast<int>(name.size()), nullptr, 0);
    if (n <= 0) return fail("Invalid SSF ZIP UTF-8 name.");
    std::wstring wide(static_cast<std::size_t>(n), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name.data(), static_cast<int>(name.size()), wide.data(), n) != n)
      return fail("Invalid SSF ZIP UTF-8 name.");
    if (!ValidateWindowsRelativePath(wide, error)) return false;
    for (const auto& previous : paths) {
      if (PathsCollide(previous.first, wide) ||
          (!previous.second && PathPrefixes(previous.first, wide)) ||
          (!directory && PathPrefixes(wide, previous.first))) return fail("Conflicting SSF ZIP paths.");
    }
    paths.emplace_back(wide, directory);
    const auto type = (attributes >> 16U) & 0170000U;
    if ((type && type != (directory ? 0040000U : 0100000U)) ||
        (attributes & FILE_ATTRIBUTE_REPARSE_POINT) || (directory && size != 0))
      return fail("SSF ZIP links and special files are forbidden.");
    if (local > central || central - local < 30 || ReadLe32(b, local) != 0x04034b50U ||
        ZipU16(b, local + 6) != flags || ZipU16(b, local + 8) != method || ZipU16(b, local + 26) != name_size)
      return fail("SSF ZIP local header mismatch.");
    const std::size_t local_extra = ZipU16(b, local + 28);
    if (name_size + local_extra > central - local - 30) return fail("SSF ZIP local name overrun.");
    if (!std::equal(raw.begin(), raw.end(), b.begin() + static_cast<std::ptrdiff_t>(local + 30),
        [](char a, std::uint8_t c) { return static_cast<std::uint8_t>(a) == c; }))
      return fail("SSF ZIP local name mismatch.");
    const std::size_t data = local + 30 + name_size + local_extra;
    if (compressed > central - data) return fail("SSF ZIP data overrun.");
    const bool descriptor = (flags & 8U) != 0;
    const auto local_crc = ReadLe32(b, local + 14), local_compressed = ReadLe32(b, local + 18), local_size = ReadLe32(b, local + 22);
    if ((!descriptor && (local_crc != crc || local_compressed != compressed || local_size != size)) ||
        (descriptor && ((local_crc && local_crc != crc) || (local_compressed && local_compressed != compressed) || (local_size && local_size != size))))
      return fail("SSF ZIP local sizes mismatch.");
    std::size_t stop = data + compressed;
    if (descriptor) {
      if (central - stop >= 4 && ReadLe32(b, stop) == 0x08074b50U) stop += 4;
      if (central - stop < 12 || ReadLe32(b, stop) != crc ||
          ReadLe32(b, stop + 4) != compressed || ReadLe32(b, stop + 8) != size)
        return fail("SSF ZIP descriptor mismatch.");
      stop += 12;
    }
    ranges.emplace_back(local, stop);
    std::vector<std::uint8_t> decoded;
    decoded.reserve(size);
    if (method == 0) decoded.assign(b.begin() + static_cast<std::ptrdiff_t>(data), b.begin() + static_cast<std::ptrdiff_t>(data + compressed));
    else if (!InflateDeflate(b.subspan(data, compressed), size, decoded, error)) return false;
    if (ZipCrc(decoded) != crc) return fail("SSF ZIP CRC mismatch.");
    if (!directory) entries.push_back({name, std::move(decoded)});
    cursor += 46 + name_size + extra + comment;
  }
  if (cursor != end) return fail("SSF ZIP trailing directory data.");
  std::sort(ranges.begin(), ranges.end());
  std::size_t next = 0;
  for (const auto& r : ranges) {
    if (r.first != next) return fail("SSF ZIP overlapping or undeclared local data.");
    next = r.second;
  }
  return next == central || fail("SSF ZIP local data does not end at its directory.");
}

static SogouSsfDecodeResult DecodeArchiveImpl(
    const std::filesystem::path& source_path, bool allow_zip) {
  SogouSsfDecodeResult result;
  try {
    std::vector<std::uint8_t> archive;
    if (!ReadArchive(source_path, archive, result.error)) {
      return result;
    }
    const std::span<const std::uint8_t> archive_view(archive);
    std::array<unsigned char, 32> hash{};
    if (BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0, archive.data(),
                   static_cast<ULONG>(archive.size()), hash.data(), static_cast<ULONG>(hash.size())) < 0) {
      result.error = "SSF SHA-256 failed.";
      return result;
    }
    constexpr char digits[] = "0123456789abcdef";
    for (const auto b : hash) { result.package_sha256 += digits[b >> 4U]; result.package_sha256 += digits[b & 15U]; }
    if (allow_zip && ReadLe32(archive_view, 0) == 0x04034b50U) {
      if (!DecodeZip(archive_view, result.entries, result.error)) result.entries.clear();
      return result;
    }
    if (archive_view[0] != static_cast<std::uint8_t>('S') ||
        archive_view[1] != static_cast<std::uint8_t>('k') ||
        archive_view[2] != static_cast<std::uint8_t>('i') ||
        archive_view[3] != static_cast<std::uint8_t>('n')) {
      result.error = "SSF 文件头不是“Skin”。";
      return result;
    }
    if (ReadLe32(archive_view, 4) != 3) {
      result.error = "仅支持当前 Sogou SSF Skin v3 格式。";
      return result;
    }

    std::vector<std::uint8_t> plaintext;
    if (!DecryptPayload(archive_view.subspan(kHeaderBytes), plaintext,
                        result.error)) {
      return result;
    }
    archive.clear();
    archive.shrink_to_fit();
    if (plaintext.size() < 4U) {
      result.error = "SSF 解密结果缺少 zlib 精确输出长度。";
      return result;
    }

    const std::size_t inflated_size = ReadLe32(plaintext, 0);
    if (inflated_size > kMaximumInflatedBytes) {
      result.error = "SSF 解压结果超过 64 MiB 安全上限。";
      return result;
    }
    std::vector<std::uint8_t> blob;
    if (!InflateZlib(std::span<const std::uint8_t>(plaintext).subspan(4),
                     inflated_size, blob, result.error)) {
      return result;
    }
    plaintext.clear();
    plaintext.shrink_to_fit();
    if (!ParseOutputBlob(blob, result.entries, result.error)) {
      result.entries.clear();
      return result;
    }
    return result;
  } catch (const std::bad_alloc&) {
    result.entries.clear();
    result.error = "内存不足，SSF 解码已安全终止。";
    return result;
  } catch (const std::exception& exception) {
    result.entries.clear();
    result.error = std::string("SSF 解码异常：") + exception.what();
    return result;
  } catch (...) {
    result.entries.clear();
    result.error = "SSF 解码发生未知异常。";
    return result;
  }
}

SogouSsfDecodeResult DecodeSogouSsfV3(const std::filesystem::path& source_path) {
  return DecodeArchiveImpl(source_path, false);
}

SogouSsfDecodeResult DecodeSogouSsfArchive(const std::filesystem::path& source_path) {
  return DecodeArchiveImpl(source_path, true);
}

}  // namespace ziliu::settings
