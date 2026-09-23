#pragma once

#include "ziliu/core/engine.h"
#include "ziliu/core/ipc_protocol.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>

namespace ziliu::core {

class SessionHost final {
 public:
  using EngineFactory = std::function<std::unique_ptr<Engine>(bool restricted)>;

  explicit SessionHost(EngineFactory engine_factory = CreateStubEngineForSession);

  [[nodiscard]] ipc::Response Handle(const ipc::Request& request);
  [[nodiscard]] std::size_t session_count() const noexcept { return sessions_.size(); }

 private:
  [[nodiscard]] std::uint64_t CreateSession(bool restricted);

  EngineFactory engine_factory_;
  std::unordered_map<std::uint64_t, std::unique_ptr<Engine>> sessions_;
  std::uint64_t next_session_id_ = 1;
};

}  // namespace ziliu::core
