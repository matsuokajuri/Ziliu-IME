#include "ziliu/core/engine.h"
#include "ziliu/core/ipc_protocol.h"
#include "ziliu/core/settings.h"
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

  const auto selected = engine->Select(0);
  Expect(selected == ziliu::core::SelectionResult{true, L"字流"},
         "select should return the committed candidate");
  Expect(engine->Snapshot().empty(), "selection should clear the composition");

  const ziliu::core::CompositionSnapshot spaced_preedit{L"你 hao", {}, 0};
  Expect(spaced_preedit.plain_text() == L"你hao",
         "plain preedit text should retain selections and remove segmentation spaces");

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

  const ziliu::core::Settings defaults;
  Expect(defaults.candidate_layout == ziliu::core::CandidateLayout::kVertical &&
             defaults.candidate_count == 5 &&
             defaults.input_mode_switch_key == ziliu::core::InputModeSwitchKey::kShift &&
             defaults.punctuation_style == ziliu::core::PunctuationStyle::kFullWidth &&
             defaults.auto_pair_punctuation &&
             defaults.page_key_set == ziliu::core::PageKeySet::kCommaPeriod &&
             defaults.default_input_mode == ziliu::core::DefaultInputMode::kChinese &&
             defaults.theme_mode == ziliu::core::ThemeMode::kSystem &&
             defaults.candidate_page_mode == ziliu::core::CandidatePageMode::kSingleLine,
         "settings defaults should match the first-run experience");

  const auto parsed_settings = ziliu::core::ParseSettings(
      "candidate_layout=horizontal\n"
      "candidate_count=7\n"
      "input_mode_switch_key=control\n"
      "punctuation_style=half_width\n"
      "auto_pair_punctuation=false\n"
      "page_keys=brackets\n"
      "character_set=traditional\n"
      "default_input_mode=english\n"
      "fuzzy_z_zh=true\n"
      "smart_numeric_punctuation=false\n"
      "theme_mode=dark\n"
      "candidate_page_mode=multi_line\n"
      "candidate_font_family=microsoft_yahei\n"
      "candidate_color_scheme=blue\n"
      "candidate_font_size=20\n"
      "candidate_scale_with_text=false\n");
  Expect(parsed_settings.candidate_layout == ziliu::core::CandidateLayout::kHorizontal &&
             parsed_settings.candidate_count == 7 &&
             parsed_settings.input_mode_switch_key ==
                 ziliu::core::InputModeSwitchKey::kControl &&
             parsed_settings.punctuation_style == ziliu::core::PunctuationStyle::kHalfWidth &&
             !parsed_settings.auto_pair_punctuation &&
             parsed_settings.page_key_set == ziliu::core::PageKeySet::kBrackets &&
             parsed_settings.character_set == ziliu::core::CharacterSet::kTraditional &&
             parsed_settings.default_input_mode == ziliu::core::DefaultInputMode::kEnglish &&
             parsed_settings.fuzzy_z_zh && !parsed_settings.smart_numeric_punctuation &&
             parsed_settings.theme_mode == ziliu::core::ThemeMode::kDark &&
             parsed_settings.candidate_page_mode ==
                 ziliu::core::CandidatePageMode::kMultiLine &&
             parsed_settings.candidate_font_family ==
                 ziliu::core::CandidateFontFamily::kMicrosoftYaHei &&
             parsed_settings.candidate_color_scheme ==
                 ziliu::core::CandidateColorScheme::kBlue &&
             parsed_settings.candidate_font_size == 20 &&
             !parsed_settings.candidate_scale_with_text,
         "settings parser should preserve all supported choices");
  Expect(ziliu::core::ParseSettings(ziliu::core::SerializeSettings(parsed_settings)) ==
             parsed_settings,
         "settings should survive a deterministic serialization round trip");
  Expect(ziliu::core::ParseSettings("candidate_count=99\n").candidate_count ==
             ziliu::core::kMaximumCandidateCount,
         "candidate count should be clamped to the supported range");
  Expect(ziliu::core::ParseSettings("candidate_font_size=99\n").candidate_font_size ==
             ziliu::core::kMaximumCandidateFontSize,
         "candidate font size should be clamped to the supported range");
  Expect(ziliu::core::MakeCandidatePageSlice(9, 5, 0) ==
             ziliu::core::CandidatePageSlice{0, 5} &&
             ziliu::core::MakeCandidatePageSlice(9, 5, 5) ==
                 ziliu::core::CandidatePageSlice{5, 4} &&
             ziliu::core::MakeCandidatePageSlice(9, 5, 99) ==
                 ziliu::core::CandidatePageSlice{5, 4},
         "candidate pagination should clamp to a stable visible slice");

  std::cout << "ziliu_core_tests: OK\n";
  return EXIT_SUCCESS;
}
