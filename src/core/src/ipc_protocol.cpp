#include "ziliu/core/ipc_protocol.h"

#include <bit>
#include <limits>
#include <string_view>
#include <type_traits>

namespace ziliu::core::ipc {
namespace {

constexpr std::uint32_t kRequestMagic = 0x31514C5AU;
constexpr std::uint32_t kResponseMagic = 0x31524C5AU;
constexpr std::size_t kMaximumStringBytes = 16U * 1024U;

class Writer final {
 public:
  template <typename Value>
    requires std::is_unsigned_v<Value>
  void Integer(Value value) {
    for (std::size_t index = 0; index < sizeof(Value); ++index) {
      bytes_.push_back(static_cast<std::byte>(value & static_cast<Value>(0xFFU)));
      value >>= 8U;
    }
  }

  bool String(std::wstring_view value) {
    std::string utf8;
    if (!ToUtf8(value, &utf8) || utf8.size() > kMaximumStringBytes ||
        utf8.size() > std::numeric_limits<std::uint32_t>::max()) {
      return false;
    }
    Integer(static_cast<std::uint32_t>(utf8.size()));
    for (const char character : utf8) {
      bytes_.push_back(static_cast<std::byte>(static_cast<unsigned char>(character)));
    }
    return true;
  }

  [[nodiscard]] std::vector<std::byte> Take() && { return std::move(bytes_); }

  bool Utf8String(std::string_view value) {
    if (value.size() > kMaximumStringBytes) {
      return false;
    }
    Integer(static_cast<std::uint32_t>(value.size()));
    for (const unsigned char character : value) {
      bytes_.push_back(static_cast<std::byte>(character));
    }
    return true;
  }

 private:
  static bool AppendCodePoint(std::uint32_t code_point, std::string* output) {
    if (code_point <= 0x7FU) {
      output->push_back(static_cast<char>(code_point));
    } else if (code_point <= 0x7FFU) {
      output->push_back(static_cast<char>(0xC0U | (code_point >> 6U)));
      output->push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
    } else if (code_point <= 0xFFFFU) {
      if (code_point >= 0xD800U && code_point <= 0xDFFFU) {
        return false;
      }
      output->push_back(static_cast<char>(0xE0U | (code_point >> 12U)));
      output->push_back(static_cast<char>(0x80U | ((code_point >> 6U) & 0x3FU)));
      output->push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
    } else if (code_point <= 0x10FFFFU) {
      output->push_back(static_cast<char>(0xF0U | (code_point >> 18U)));
      output->push_back(static_cast<char>(0x80U | ((code_point >> 12U) & 0x3FU)));
      output->push_back(static_cast<char>(0x80U | ((code_point >> 6U) & 0x3FU)));
      output->push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
    } else {
      return false;
    }
    return true;
  }

  static bool ToUtf8(std::wstring_view value, std::string* output) {
    output->clear();
    for (std::size_t index = 0; index < value.size(); ++index) {
      std::uint32_t code_point = static_cast<std::uint32_t>(value[index]);
      if constexpr (sizeof(wchar_t) == 2) {
        if (code_point >= 0xD800U && code_point <= 0xDBFFU) {
          if (++index >= value.size()) {
            return false;
          }
          const std::uint32_t low = static_cast<std::uint32_t>(value[index]);
          if (low < 0xDC00U || low > 0xDFFFU) {
            return false;
          }
          code_point = 0x10000U + ((code_point - 0xD800U) << 10U) + (low - 0xDC00U);
        } else if (code_point >= 0xDC00U && code_point <= 0xDFFFU) {
          return false;
        }
      }
      if (!AppendCodePoint(code_point, output)) {
        return false;
      }
    }
    return true;
  }

  std::vector<std::byte> bytes_;
};

class Reader final {
 public:
  explicit Reader(std::span<const std::byte> bytes) : bytes_(bytes) {}

  template <typename Value>
    requires std::is_unsigned_v<Value>
  bool Integer(Value* value) {
    if (value == nullptr || remaining() < sizeof(Value)) {
      return false;
    }
    Value decoded = 0;
    for (std::size_t index = 0; index < sizeof(Value); ++index) {
      decoded |= static_cast<Value>(std::to_integer<unsigned int>(bytes_[offset_ + index]))
                 << (index * 8U);
    }
    offset_ += sizeof(Value);
    *value = decoded;
    return true;
  }

  bool String(std::wstring* value) {
    std::uint32_t byte_count = 0;
    if (value == nullptr || !Integer(&byte_count) || byte_count > kMaximumStringBytes ||
        remaining() < byte_count) {
      return false;
    }
    const auto data = bytes_.subspan(offset_, byte_count);
    offset_ += byte_count;
    return FromUtf8(data, value);
  }

  [[nodiscard]] bool finished() const noexcept { return offset_ == bytes_.size(); }

  bool Utf8String(std::string* value) {
    std::uint32_t count = 0;
    if (value == nullptr || !Integer(&count) || count > kMaximumStringBytes || remaining() < count) {
      return false;
    }
    const auto data = bytes_.subspan(offset_, count);
    std::wstring validated;
    if (!FromUtf8(data, &validated)) {
      return false;
    }
    value->assign(reinterpret_cast<const char*>(data.data()), data.size());
    offset_ += count;
    return true;
  }

 private:
  [[nodiscard]] std::size_t remaining() const noexcept { return bytes_.size() - offset_; }

  static bool FromUtf8(std::span<const std::byte> data, std::wstring* output) {
    output->clear();
    for (std::size_t index = 0; index < data.size();) {
      const auto first = std::to_integer<std::uint8_t>(data[index++]);
      std::uint32_t code_point = 0;
      std::size_t continuation_count = 0;
      if (first <= 0x7FU) {
        code_point = first;
      } else if ((first & 0xE0U) == 0xC0U) {
        code_point = first & 0x1FU;
        continuation_count = 1;
      } else if ((first & 0xF0U) == 0xE0U) {
        code_point = first & 0x0FU;
        continuation_count = 2;
      } else if ((first & 0xF8U) == 0xF0U) {
        code_point = first & 0x07U;
        continuation_count = 3;
      } else {
        return false;
      }
      if (index + continuation_count > data.size()) {
        return false;
      }
      for (std::size_t continuation = 0; continuation < continuation_count; ++continuation) {
        const auto next = std::to_integer<std::uint8_t>(data[index++]);
        if ((next & 0xC0U) != 0x80U) {
          return false;
        }
        code_point = (code_point << 6U) | (next & 0x3FU);
      }
      const std::uint32_t minimum = continuation_count == 0   ? 0U
                                    : continuation_count == 1 ? 0x80U
                                    : continuation_count == 2 ? 0x800U
                                                              : 0x10000U;
      if (code_point < minimum || code_point > 0x10FFFFU ||
          (code_point >= 0xD800U && code_point <= 0xDFFFU)) {
        return false;
      }
      if constexpr (sizeof(wchar_t) == 2) {
        if (code_point > 0xFFFFU) {
          code_point -= 0x10000U;
          output->push_back(static_cast<wchar_t>(0xD800U + (code_point >> 10U)));
          output->push_back(static_cast<wchar_t>(0xDC00U + (code_point & 0x3FFU)));
        } else {
          output->push_back(static_cast<wchar_t>(code_point));
        }
      } else {
        output->push_back(static_cast<wchar_t>(code_point));
      }
    }
    return true;
  }

  std::span<const std::byte> bytes_;
  std::size_t offset_ = 0;
};

bool IsKnownCommand(Command command) {
  switch (command) {
    case Command::kPing:
    case Command::kCreateSession:
    case Command::kCloseSession:
    case Command::kReset:
    case Command::kInputLetter:
    case Command::kBackspace:
    case Command::kSelectCandidate:
    case Command::kPageUp:
    case Command::kPageDown:
    case Command::kSetTraditional:
    case Command::kSetCandidatePageSize:
    case Command::kSetChineseCandidatesOnly:
    case Command::kInputSeparator:
    case Command::kSetCandidateWindowPageCount:
    case Command::kGetSettings:
      return true;
  }
  return false;
}

bool IsKnownStatus(Status status) {
  switch (status) {
    case Status::kOk:
    case Status::kInvalidRequest:
    case Status::kSessionNotFound:
    case Status::kUnsupported:
    case Status::kInternalError:
      return true;
  }
  return false;
}

bool IsSupportedProtocolVersion(std::uint16_t version) {
  return version >= kOldestCompatibleProtocolVersion &&
         version <= kProtocolVersion;
}

}  // namespace

bool EncodeRequest(const Request& request, std::vector<std::byte>* bytes) {
  return EncodeRequest(request, kProtocolVersion, bytes);
}

bool EncodeRequest(const Request& request, std::uint16_t protocol_version,
                   std::vector<std::byte>* bytes) {
  if (bytes == nullptr || !IsKnownCommand(request.command)) {
    return false;
  }
  if (!IsSupportedProtocolVersion(protocol_version) ||
      (request.command == Command::kGetSettings && protocol_version < 6)) {
    return false;
  }
  Writer writer;
  writer.Integer(kRequestMagic);
  writer.Integer(protocol_version);
  writer.Integer(static_cast<std::uint16_t>(request.command));
  writer.Integer(request.request_id);
  writer.Integer(request.session_id);
  writer.Integer(request.value);
  *bytes = std::move(writer).Take();
  return bytes->size() <= kMaximumMessageBytes;
}

bool DecodeRequest(std::span<const std::byte> bytes, Request* request) {
  return DecodeRequest(bytes, request, nullptr);
}

bool DecodeRequest(std::span<const std::byte> bytes, Request* request,
                   std::uint16_t* protocol_version) {
  if (request == nullptr || bytes.size() > kMaximumMessageBytes) {
    return false;
  }
  Reader reader(bytes);
  std::uint32_t magic = 0;
  std::uint16_t version = 0;
  std::uint16_t command = 0;
  Request decoded;
  if (!reader.Integer(&magic) || !reader.Integer(&version) || !reader.Integer(&command) ||
      !reader.Integer(&decoded.request_id) || !reader.Integer(&decoded.session_id) ||
      !reader.Integer(&decoded.value) || !reader.finished() || magic != kRequestMagic ||
      !IsSupportedProtocolVersion(version)) {
    return false;
  }
  decoded.command = static_cast<Command>(command);
  if (!IsKnownCommand(decoded.command) ||
      (decoded.command == Command::kGetSettings && version < 6)) {
    return false;
  }
  *request = decoded;
  if (protocol_version != nullptr) {
    *protocol_version = version;
  }
  return true;
}

bool EncodeResponse(const Response& response, std::vector<std::byte>* bytes) {
  return EncodeResponse(response, kProtocolVersion, bytes);
}

bool EncodeResponse(const Response& response, std::uint16_t protocol_version,
                    std::vector<std::byte>* bytes) {
  if (bytes == nullptr || !IsKnownStatus(response.status) ||
      response.snapshot.candidates.size() > kMaximumCandidates ||
      !IsSupportedProtocolVersion(protocol_version)) {
    return false;
  }
  Writer writer;
  writer.Integer(kResponseMagic);
  writer.Integer(protocol_version);
  writer.Integer(static_cast<std::uint16_t>(response.status));
  writer.Integer(response.request_id);
  writer.Integer(response.session_id);
  writer.Integer(static_cast<std::uint8_t>(response.consumed ? 1U : 0U));
  if (!writer.String(response.commit) || !writer.String(response.snapshot.preedit)) {
    return false;
  }
  writer.Integer(static_cast<std::uint32_t>(response.snapshot.highlighted_index));
  if (protocol_version >= 5) {
    writer.Integer(static_cast<std::uint8_t>(
        response.snapshot.has_previous_page ? 1U : 0U));
    writer.Integer(static_cast<std::uint8_t>(
        response.snapshot.has_next_page ? 1U : 0U));
  }
  writer.Integer(static_cast<std::uint32_t>(response.snapshot.candidates.size()));
  for (const auto& candidate : response.snapshot.candidates) {
    if (!writer.String(candidate.text) || !writer.String(candidate.annotation)) {
      return false;
    }
    writer.Integer(std::bit_cast<std::uint64_t>(candidate.score));
  }
  if (protocol_version >= 6 && !writer.Utf8String(response.settings_text)) {
    return false;
  }
  *bytes = std::move(writer).Take();
  return bytes->size() <= kMaximumMessageBytes;
}

bool DecodeResponse(std::span<const std::byte> bytes, Response* response) {
  return DecodeResponse(bytes, response, nullptr);
}

bool DecodeResponse(std::span<const std::byte> bytes, Response* response,
                    std::uint16_t* protocol_version) {
  if (response == nullptr || bytes.size() > kMaximumMessageBytes) {
    return false;
  }
  Reader reader(bytes);
  std::uint32_t magic = 0;
  std::uint16_t version = 0;
  std::uint16_t status = 0;
  std::uint8_t consumed = 0;
  std::uint8_t has_previous_page = 0;
  std::uint8_t has_next_page = 0;
  std::uint32_t highlighted = 0;
  std::uint32_t candidate_count = 0;
  Response decoded;
  if (!reader.Integer(&magic) || !reader.Integer(&version) ||
      !IsSupportedProtocolVersion(version) || !reader.Integer(&status) ||
      !reader.Integer(&decoded.request_id) || !reader.Integer(&decoded.session_id) ||
      !reader.Integer(&consumed) || consumed > 1U || !reader.String(&decoded.commit) ||
      !reader.String(&decoded.snapshot.preedit) || !reader.Integer(&highlighted)) {
    return false;
  }
  if (version >= 5 &&
      (!reader.Integer(&has_previous_page) || has_previous_page > 1U ||
       !reader.Integer(&has_next_page) || has_next_page > 1U)) {
    return false;
  }
  if (!reader.Integer(&candidate_count) || magic != kResponseMagic ||
      candidate_count > kMaximumCandidates) {
    return false;
  }
  decoded.status = static_cast<Status>(status);
  if (!IsKnownStatus(decoded.status)) {
    return false;
  }
  decoded.consumed = consumed != 0;
  decoded.snapshot.highlighted_index = highlighted;
  decoded.snapshot.has_previous_page = has_previous_page != 0;
  decoded.snapshot.has_next_page = has_next_page != 0;
  decoded.snapshot.candidates.reserve(candidate_count);
  for (std::uint32_t index = 0; index < candidate_count; ++index) {
    Candidate candidate;
    std::uint64_t score = 0;
    if (!reader.String(&candidate.text) || !reader.String(&candidate.annotation) ||
        !reader.Integer(&score)) {
      return false;
    }
    candidate.score = std::bit_cast<double>(score);
    decoded.snapshot.candidates.push_back(std::move(candidate));
  }
  if ((version >= 6 && !reader.Utf8String(&decoded.settings_text)) || !reader.finished() ||
      (!decoded.snapshot.candidates.empty() &&
       decoded.snapshot.highlighted_index >= decoded.snapshot.candidates.size())) {
    return false;
  }
  *response = std::move(decoded);
  if (protocol_version != nullptr) {
    *protocol_version = version;
  }
  return true;
}

}  // namespace ziliu::core::ipc
