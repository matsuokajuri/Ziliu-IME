#include "ziliu/broker/rime_engine.h"
#include "ziliu/core/settings.h"
#include "ziliu/core/theme_catalog.h"
#include "ziliu/ipc/pipe_server.h"

#include <windows.h>
#include <shellapi.h>

#include <array>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr wchar_t kBrokerMutexName[] = L"Local\\Ziliu.Broker.Singleton.v1";

std::optional<std::filesystem::path> SettingsFilePath() {
  std::array<wchar_t, 32768> local_data{};
  const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", local_data.data(),
                                               static_cast<DWORD>(local_data.size()));
  if (length == 0 || length >= local_data.size()) {
    return std::nullopt;
  }
  return std::filesystem::path(local_data.data()) / L"Ziliu" / L"settings.ini";
}

std::optional<std::string> ReadSettingsForClient() {
  // The client supplies no path. Only this user's canonical settings are exposed,
  // after PipeServer's same-user/session authentication and impersonation cleanup.
  const auto path = SettingsFilePath();
  if (!path.has_value()) {
    return std::nullopt;
  }
  std::error_code error;
  if (!std::filesystem::exists(*path, error)) {
    if (error) {
      return std::nullopt;
    }
    return ziliu::core::SerializeSettings({});
  }
  std::ifstream stream(*path, std::ios::binary);
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

std::optional<std::vector<std::byte>> ReadActiveThemeResource(
    std::string_view theme_id, std::string_view resource, std::uint32_t offset) {
  const auto settings_text = ReadSettingsForClient();
  if (!settings_text.has_value() ||
      ziliu::core::ParseSettings(*settings_text).active_theme_id != theme_id ||
      (!resource.empty() && !ziliu::core::IsSafeThemeAssetPath(resource))) {
    return std::nullopt;
  }
  std::array<wchar_t, 32768> local_data{};
  const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", local_data.data(),
                                               static_cast<DWORD>(local_data.size()));
  if (length == 0 || length >= local_data.size()) {
    return std::nullopt;
  }
  const auto themes_directory = std::filesystem::path(local_data.data()) / L"Ziliu" / L"Themes";
  const auto installed = ziliu::core::LoadInstalledTheme(themes_directory, theme_id);
  if (!installed.has_value()) {
    return std::nullopt;
  }
  std::filesystem::path path = installed->directory;
  if (resource.empty()) {
    path /= L"manifest.json";
  } else {
    const auto relative = std::filesystem::path(std::u8string_view(
        reinterpret_cast<const char8_t*>(resource.data()), resource.size()));
    for (const auto& part : relative) {
      path /= part;
      std::error_code error;
      if (std::filesystem::is_symlink(std::filesystem::symlink_status(path, error)) || error) {
        return std::nullopt;
      }
    }
  }
  std::error_code error;
  const auto status = std::filesystem::symlink_status(path, error);
  if (error || !std::filesystem::is_regular_file(status) || std::filesystem::is_symlink(status)) {
    return std::nullopt;
  }
  const auto size = std::filesystem::file_size(path, error);
  const std::size_t limit = resource.empty() ? ziliu::core::kMaximumThemeManifestBytes
                                              : ziliu::core::kMaximumThemeAssetBytes;
  if (error || size > limit || offset > size) {
    return std::nullopt;
  }
  const auto count = static_cast<std::size_t>(std::min<std::uintmax_t>(
      size - offset, ziliu::core::ipc::kMaximumThemeChunkBytes));
  std::vector<std::byte> bytes(count);
  if (count == 0) {
    return bytes;
  }
  std::ifstream stream(path, std::ios::binary);
  if (!stream || !stream.seekg(offset) ||
      !stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(count))) {
    return std::nullopt;
  }
  return bytes;
}

bool OpenQuickMenuForClient(std::int32_t x, std::int32_t y) {
  std::array<wchar_t, 32768> module_path{};
  const DWORD length = GetModuleFileNameW(nullptr, module_path.data(),
                                          static_cast<DWORD>(module_path.size()));
  if (length == 0 || length >= module_path.size()) {
    return false;
  }
  const auto directory = std::filesystem::path(module_path.data()).parent_path();
  const auto settings = directory / L"ZiliuSettings.exe";
  std::error_code error;
  if (!std::filesystem::is_regular_file(settings, error) || error) {
    return false;
  }
  const std::wstring arguments =
      L"--quick-menu --x " + std::to_wstring(x) + L" --y " + std::to_wstring(y);
  const HINSTANCE launched = ShellExecuteW(nullptr, L"open", settings.c_str(),
                                           arguments.c_str(), directory.c_str(), SW_SHOWNORMAL);
  return reinterpret_cast<INT_PTR>(launched) > 32;
}

bool RunMenuActionForClient(std::uint32_t action) {
  if (action == 1) {
    const auto path = SettingsFilePath();
    const auto contents = ReadSettingsForClient();
    if (!path.has_value() || !contents.has_value()) {
      return false;
    }
    auto settings = ziliu::core::ParseSettings(*contents);
    settings.character_set = settings.character_set == ziliu::core::CharacterSet::kSimplified
                                 ? ziliu::core::CharacterSet::kTraditional
                                 : ziliu::core::CharacterSet::kSimplified;
    std::error_code error;
    std::filesystem::create_directories(path->parent_path(), error);
    if (error) {
      return false;
    }
    auto temporary = *path;
    temporary += L".broker.tmp";
    std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
    const auto serialized = ziliu::core::SerializeSettings(settings);
    stream.write(serialized.data(), static_cast<std::streamsize>(serialized.size()));
    stream.flush();
    const bool written = stream.good();
    stream.close();
    if (!written || stream.fail() ||
        !MoveFileExW(temporary.c_str(), path->c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
      std::filesystem::remove(temporary, error);
      return false;
    }
    return true;
  }
  if (action == 2) {
    std::array<wchar_t, 32768> module_path{};
    const DWORD length = GetModuleFileNameW(nullptr, module_path.data(),
                                            static_cast<DWORD>(module_path.size()));
    if (length == 0 || length >= module_path.size()) {
      return false;
    }
    const auto directory = std::filesystem::path(module_path.data()).parent_path();
    const auto executable = directory / L"ZiliuSettings.exe";
    std::error_code error;
    if (!std::filesystem::is_regular_file(executable, error) || error) {
      return false;
    }
    const HINSTANCE launched = ShellExecuteW(nullptr, L"open", executable.c_str(), nullptr,
                                             directory.c_str(), SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(launched) > 32;
  }
  return false;
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
                               ReadSettingsForClient, ReadActiveThemeResource,
                               OpenQuickMenuForClient, RunMenuActionForClient);
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
