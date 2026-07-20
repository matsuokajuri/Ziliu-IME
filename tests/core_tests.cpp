#include "ziliu/core/engine.h"

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

  std::cout << "ziliu_core_tests: OK\n";
  return EXIT_SUCCESS;
}
