#pragma once

#include "ziliu/core/engine.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace ziliu::core::ipc {

inline constexpr std::uint16_t kProtocolVersion = 1;
inline constexpr std::size_t kMaximumMessageBytes = 64U * 1024U;
inline constexpr std::size_t kMaximumCandidates = 9;

enum class Command : std::uint16_t {
  kPing = 1,
  kCreateSession = 2,
  kCloseSession = 3,
  kReset = 4,
  kInputLetter = 5,
  kBackspace = 6,
  kSelectCandidate = 7,
};

enum class Status : std::uint16_t {
  kOk = 0,
  kInvalidRequest = 1,
  kSessionNotFound = 2,
  kUnsupported = 3,
  kInternalError = 4,
};

struct Request {
  std::uint64_t request_id = 0;
  std::uint64_t session_id = 0;
  Command command = Command::kPing;
  std::uint32_t value = 0;
};

struct Response {
  std::uint64_t request_id = 0;
  std::uint64_t session_id = 0;
  Status status = Status::kOk;
  bool consumed = false;
  std::wstring commit;
  CompositionSnapshot snapshot;
};

[[nodiscard]] bool EncodeRequest(const Request& request, std::vector<std::byte>* bytes);
[[nodiscard]] bool DecodeRequest(std::span<const std::byte> bytes, Request* request);
[[nodiscard]] bool EncodeResponse(const Response& response, std::vector<std::byte>* bytes);
[[nodiscard]] bool DecodeResponse(std::span<const std::byte> bytes, Response* response);

}  // namespace ziliu::core::ipc
