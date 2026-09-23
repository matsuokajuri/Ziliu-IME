#pragma once

#include "ziliu/core/engine.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace ziliu::core::ipc {

inline constexpr std::uint16_t kProtocolVersion = 8;
inline constexpr std::uint16_t kOldestCompatibleProtocolVersion = 4;
inline constexpr std::size_t kMaximumMessageBytes = 64U * 1024U;
inline constexpr std::size_t kMaximumThemeChunkBytes = 60U * 1024U;
inline constexpr std::size_t kMaximumCandidatesPerPage = 9;
inline constexpr std::size_t kMaximumCandidateWindowPages = 5;
inline constexpr std::size_t kMaximumCandidates =
    kMaximumCandidatesPerPage * kMaximumCandidateWindowPages;

enum class Command : std::uint16_t {
  kPing = 1,
  kCreateSession = 2,
  kCloseSession = 3,
  kReset = 4,
  kInputLetter = 5,
  kBackspace = 6,
  kSelectCandidate = 7,
  kPageUp = 8,
  kPageDown = 9,
  kSetTraditional = 10,
  kSetCandidatePageSize = 11,
  kSetChineseCandidatesOnly = 12,
  kInputSeparator = 13,
  kSetCandidateWindowPageCount = 14,
  kGetSettings = 15,
  kGetThemeResource = 16,
  kOpenQuickMenu = 17,
  kRunMenuAction = 18,
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
  // Protocol v7: kCreateSession uses 0 for ordinary and 1 for restricted.
  std::uint32_t value = 0;
  // Protocol v8: resource is empty for manifest.json, otherwise a validated
  // relative asset path. The broker only serves the currently active theme.
  std::string theme_id;
  std::string resource;
  std::int32_t point_x = 0;
  std::int32_t point_y = 0;
};

struct Response {
  std::uint64_t request_id = 0;
  std::uint64_t session_id = 0;
  Status status = Status::kOk;
  bool consumed = false;
  std::wstring commit;
  CompositionSnapshot snapshot;
  std::string settings_text;  // Protocol v6: bounded UTF-8 serialized settings.
  std::vector<std::byte> theme_chunk;  // Protocol v8: bounded raw theme bytes.
};

[[nodiscard]] bool EncodeRequest(const Request& request, std::vector<std::byte>* bytes);
[[nodiscard]] bool EncodeRequest(const Request& request, std::uint16_t protocol_version,
                                 std::vector<std::byte>* bytes);
[[nodiscard]] bool DecodeRequest(std::span<const std::byte> bytes, Request* request);
[[nodiscard]] bool DecodeRequest(std::span<const std::byte> bytes, Request* request,
                                 std::uint16_t* protocol_version);
[[nodiscard]] bool EncodeResponse(const Response& response, std::vector<std::byte>* bytes);
[[nodiscard]] bool EncodeResponse(const Response& response, std::uint16_t protocol_version,
                                  std::vector<std::byte>* bytes);
[[nodiscard]] bool DecodeResponse(std::span<const std::byte> bytes, Response* response);
[[nodiscard]] bool DecodeResponse(std::span<const std::byte> bytes, Response* response,
                                  std::uint16_t* protocol_version);

}  // namespace ziliu::core::ipc
