#include "ziliu/broker/rime_engine.h"
#include "ziliu/ipc/pipe_server.h"

#include <windows.h>

namespace {

constexpr wchar_t kBrokerMutexName[] = L"Local\\Ziliu.Broker.Singleton.v1";

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
  ziliu::ipc::PipeServer server(ziliu::ipc::kBrokerPipeName, ziliu::broker::CreateEngine);
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
