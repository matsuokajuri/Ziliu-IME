#pragma once

#include "ziliu/core/session_host.h"
#include "ziliu/ipc/pipe_client.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ziliu::ipc {

class PipeServer final {
 public:
  using SettingsProvider = std::function<std::optional<std::string>()>;
  using ThemeResourceProvider = std::function<std::optional<std::vector<std::byte>>(
      std::string_view theme_id, std::string_view resource, std::uint32_t offset)>;
  using QuickMenuProvider = std::function<bool(std::int32_t x, std::int32_t y)>;
  using MenuActionProvider = std::function<bool(std::uint32_t action)>;
  explicit PipeServer(std::wstring pipe_name = kBrokerPipeName,
                      core::SessionHost::EngineFactory engine_factory =
                          core::CreateStubEngineForSession,
                      SettingsProvider settings_provider = {},
                      ThemeResourceProvider theme_resource_provider = {},
                      QuickMenuProvider quick_menu_provider = {},
                      MenuActionProvider menu_action_provider = {});

  PipeServer(const PipeServer&) = delete;
  PipeServer& operator=(const PipeServer&) = delete;

  [[nodiscard]] int Run();
  void Stop();

 private:
  [[nodiscard]] bool ServeClient(void* pipe_handle);

  std::wstring pipe_name_;
  core::SessionHost session_host_;
  SettingsProvider settings_provider_;
  ThemeResourceProvider theme_resource_provider_;
  QuickMenuProvider quick_menu_provider_;
  MenuActionProvider menu_action_provider_;
  std::atomic_bool stopping_ = false;
};

}  // namespace ziliu::ipc
