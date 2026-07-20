#include "ziliu/core/engine.h"
#include "ziliu/core/ipc_protocol.h"
#include "ziliu/core/session_host.h"

#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {

void Expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

void Type(ziliu::core::Engine& engine, std::wstring_view text) {
  for (const wchar_t letter : text) {
    Expect(engine.ProcessLetter(letter), "letter should be accepted");
  }
}

}  // namespace

int main() {
  auto engine = ziliu::core::CreateStubEngine();

  Type(*engine, L"ZILIU");
  auto snapshot = engine->Snapshot();
  Expect(snapshot.preedit == L"ziliu", "preedit should be normalized to lowercase");
  Expect(!snapshot.candidates.empty(), "ziliu should have candidates");
  Expect(snapshot.candidates.front().text == L"字流", "字流 should be the first candidate");

  const auto committed = engine->Select(0);
  Expect(committed == L"字流", "select should return the committed candidate");
  Expect(engine->Snapshot().empty(), "selection should clear the composition");

  Type(*engine, L"nihao");
  Expect(engine->Backspace(), "backspace should consume an existing letter");
  Expect(engine->Snapshot().preedit == L"niha", "backspace should remove one letter");

  engine->Reset();
  Expect(!engine->Backspace(), "backspace should pass through on an empty composition");
  Expect(!engine->ProcessLetter(L'1'), "non-letters should pass through");

  ziliu::core::SessionHost host;
  const auto created = host.Handle({1, 0, ziliu::core::ipc::Command::kCreateSession, 0});
  Expect(created.session_id != 0 && host.session_count() == 1,
         "session host should create an isolated engine");
  const auto input = host.Handle(
      {2, created.session_id, ziliu::core::ipc::Command::kInputLetter, L'z'});
  Expect(input.consumed && input.snapshot.preedit == L"z",
         "session host should route input to its engine");

  ziliu::core::ipc::Response wire_response;
  wire_response.request_id = 9;
  wire_response.session_id = created.session_id;
  wire_response.consumed = true;
  wire_response.commit = L"字流";
  wire_response.snapshot = snapshot;
  std::vector<std::byte> bytes;
  Expect(ziliu::core::ipc::EncodeResponse(wire_response, &bytes),
         "response should encode");
  ziliu::core::ipc::Response decoded;
  Expect(ziliu::core::ipc::DecodeResponse(bytes, &decoded), "response should decode");
  Expect(decoded.commit == wire_response.commit &&
             decoded.snapshot.candidates == wire_response.snapshot.candidates,
         "UTF-8 protocol round trip should preserve candidates");

  std::cout << "ziliu_core_tests: OK\n";
  return EXIT_SUCCESS;
}
