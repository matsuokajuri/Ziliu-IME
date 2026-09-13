#pragma once

#include "ziliu/core/session_host.h"
#include "ziliu/ipc/pipe_client.h"

#include <atomic>
#include <functional>
#include <optional>
#include <string>

namespace ziliu::ipc {

class PipeServer final {
 public:
  using SettingsProvider = std::function<std::optional<std::string>()>;
  explicit PipeServer(std::wstring pipe_name = kBrokerPipeName,
                      core::SessionHost::EngineFactory engine_factory = core::CreateStubEngine,
                      SettingsProvider settings_provider = {});

  PipeServer(const PipeServer&) = delete;
  PipeServer& operator=(const PipeServer&) = delete;

  [[nodiscard]] int Run();
  void Stop();

 private:
  [[nodiscard]] bool ServeClient(void* pipe_handle);

  std::wstring pipe_name_;
  core::SessionHost session_host_;
  SettingsProvider settings_provider_;
  std::atomic_bool stopping_ = false;
};

}  // namespace ziliu::ipc
