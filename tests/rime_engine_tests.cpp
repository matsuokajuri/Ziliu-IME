#include "ziliu/broker/rime_engine.h"

#include <algorithm>
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

}  // namespace

int main() {
  auto engine = ziliu::broker::CreateEngine();
  for (const wchar_t letter : std::wstring_view(L"zhongguo")) {
    Expect(engine->ProcessLetter(letter), "Rime should consume a pinyin letter");
  }

  const auto snapshot = engine->Snapshot();
  const auto china = std::ranges::find_if(snapshot.candidates, [](const auto& candidate) {
    return candidate.text == L"中国";
  });
  if (china == snapshot.candidates.end()) {
    if (snapshot.candidates.size() == 1 && snapshot.candidates.front().text == L"zhongguo") {
      std::cout << "SKIPPED: verified rime.dll is not staged\n";
      return 77;
    }
    Expect(false, "Rime Ice should offer 中国 for zhongguo");
  }

  const auto candidate_index = static_cast<std::size_t>(china - snapshot.candidates.begin());
  Expect(engine->Select(candidate_index) == L"中国", "Rime should commit the selected candidate");
  Expect(engine->Snapshot().empty(), "Rime commit should clear the composition");

  for (const wchar_t letter : std::wstring_view(L"ziliu")) {
    Expect(engine->ProcessLetter(letter), "Rime should consume the Ziliu spelling");
  }
  const auto ziliu_snapshot = engine->Snapshot();
  Expect(!ziliu_snapshot.candidates.empty(), "Ziliu overlay should offer candidates");
  Expect(ziliu_snapshot.candidates.front().text == L"字流",
         "Ziliu overlay should rank 字流 first");
  const auto ziliu = std::ranges::find_if(ziliu_snapshot.candidates, [](const auto& candidate) {
    return candidate.text == L"字流";
  });
  Expect(ziliu != ziliu_snapshot.candidates.end(), "Ziliu overlay should offer 字流 for ziliu");
  Expect(engine->Select(static_cast<std::size_t>(ziliu - ziliu_snapshot.candidates.begin())) ==
             L"字流",
         "Ziliu overlay should commit 字流");
  std::cout << "ziliu_rime_engine_tests: OK\n";
  return EXIT_SUCCESS;
}
