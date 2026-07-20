#include "ziliu/core/ipc_protocol.h"
#include "ziliu/ipc/pipe_client.h"
#include "ziliu/ipc/pipe_server.h"

#include <windows.h>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

namespace {

void Expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

}  // namespace

int main() {
  using ziliu::core::ipc::Command;
  using ziliu::core::ipc::Request;
  using ziliu::core::ipc::Status;

  const std::wstring pipe_name =
      L"\\\\.\\pipe\\Ziliu.Tests." + std::to_wstring(GetCurrentProcessId());
  ziliu::ipc::PipeServer server(pipe_name);
  std::jthread server_thread([&server] { Expect(server.Run() == 0, "server should stop cleanly"); });
  ziliu::ipc::PipeClient client(pipe_name, 5000);

  std::uint64_t request_id = 1;
  std::optional<ziliu::core::ipc::Response> response;
  for (int attempt = 0; attempt < 100 && !response.has_value(); ++attempt) {
    response = client.Exchange(Request{request_id, 0, Command::kCreateSession, 0});
    if (!response.has_value()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
  }
  ++request_id;
  Expect(response.has_value(), "create session should receive a response");
  Expect(response->status == Status::kOk && response->session_id != 0,
         "create session should return an id");
  const std::uint64_t session_id = response->session_id;

  for (const wchar_t letter : std::wstring_view(L"ziliu")) {
    response = client.Exchange(
        Request{request_id++, session_id, Command::kInputLetter,
                static_cast<std::uint32_t>(letter)});
    Expect(response.has_value(), "letter request should receive a response");
    Expect(response->status == Status::kOk, "letter request should keep the session");
    Expect(response->session_id == session_id, "letter response should match the session");
    Expect(response->consumed, "letters should be consumed");
  }
  Expect(response->snapshot.preedit == L"ziliu", "preedit should cross the pipe");
  Expect(!response->snapshot.candidates.empty() &&
             response->snapshot.candidates.front().text == L"字流",
         "UTF-8 candidates should cross the pipe");

  response = client.Exchange(Request{request_id++, session_id, Command::kSelectCandidate, 0});
  Expect(response.has_value() && response->commit == L"字流", "selection should commit 字流");
  Expect(response->snapshot.empty(), "commit should clear the composition");

  response = client.Exchange(Request{request_id++, session_id, Command::kCloseSession, 0});
  Expect(response.has_value() && response->status == Status::kOk,
         "close session should succeed");

  server.Stop();
  server_thread.join();
  std::cout << "ziliu_ipc_tests: OK\n";
  return EXIT_SUCCESS;
}
