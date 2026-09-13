#include "ziliu/broker/rime_engine.h"
#include "ziliu/core/settings.h"
#include "ziliu/ipc/pipe_server.h"

#include <windows.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

namespace {

constexpr wchar_t kBrokerMutexName[] = L"Local\\Ziliu.Broker.Singleton.v1";

std::optional<std::string> ReadSettingsForClient() {
  // The client supplies no path. Only this user's canonical settings are exposed,
  // after PipeServer's same-user/session authentication and impersonation cleanup.
  std::array<wchar_t, 32768> local_data{};
  const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", local_data.data(),
                                               static_cast<DWORD>(local_data.size()));
  if (length == 0 || length >= local_data.size()) {
    return std::nullopt;
  }
  const auto path = std::filesystem::path(local_data.data()) / L"Ziliu" / L"settings.ini";
  std::error_code error;
  if (!std::filesystem::exists(path, error)) {
    if (error) {
      return std::nullopt;
    }
    return ziliu::core::SerializeSettings({});
  }
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    return std::nullopt;
  }
  std::array<char, 16385> buffer{};
  stream.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
  if (stream.bad() || stream.gcount() == static_cast<std::streamsize>(buffer.size())) {
    return std::nullopt;
  }
  return ziliu::core::SerializeSettings(ziliu::core::ParseSettings(
      std::string_view(buffer.data(), static_cast<std::size_t>(stream.gcount()))));
}

int RunBroker() {
  HANDLE mutex = CreateMutexW(nullptr, TRUE, kBrokerMutexName);
  if (mutex == nullptr) {
    return 1;
  }
  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    CloseHandle(mutex);
    return 0;
  }

  ziliu::broker::WarmUpEngineRuntime();
  ziliu::ipc::PipeServer server(ziliu::ipc::kBrokerPipeName, ziliu::broker::CreateEngine,
                               ReadSettingsForClient);
  const int result = server.Run();
  CloseHandle(mutex);
  return result;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous_instance, wchar_t* command_line,
                    int show_command) {
  static_cast<void>(instance);
  static_cast<void>(previous_instance);
  static_cast<void>(command_line);
  static_cast<void>(show_command);
  return RunBroker();
}
