#pragma once

#include "ziliu/core/ipc_protocol.h"

#include <cstdint>
#include <optional>
#include <string>

namespace ziliu::ipc {

inline constexpr wchar_t kBrokerPipeName[] = L"\\\\.\\pipe\\Ziliu.Broker.v1";

class PipeClient final {
 public:
  explicit PipeClient(std::wstring pipe_name = kBrokerPipeName,
                      std::uint32_t timeout_milliseconds = 8);

  [[nodiscard]] std::optional<core::ipc::Response> Exchange(
      const core::ipc::Request& request) const;

 private:
  std::wstring pipe_name_;
  std::uint32_t timeout_milliseconds_;
};

}  // namespace ziliu::ipc
