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
  const ziliu::core::CompositionSnapshot separated_preedit{L"xi'an", {}, 0};
  Expect(separated_preedit.plain_text() == L"xian",
         "plain preedit text should omit pinyin separators");

  Type(*engine, L"ni");
  Expect(engine->ProcessSeparator(), "manual pinyin separator should be accepted");
  Type(*engine, L"hao");
  snapshot = engine->Snapshot();
  Expect(snapshot.preedit == L"ni'hao" && !snapshot.candidates.empty() &&
             snapshot.candidates.front().text == L"你好",
         "manual pinyin separator should preserve candidate lookup");
  engine->Reset();

  Expect(ziliu::core::IsChineseCandidate(L"中文") &&
             ziliu::core::IsChineseCandidate(L"繁體") &&
             ziliu::core::IsChineseCandidate(L"〇") &&
             ziliu::core::IsChineseCandidate(L"𠀀") &&
             !ziliu::core::IsChineseCandidate(L"") &&
             !ziliu::core::IsChineseCandidate(L"hello") &&
             !ziliu::core::IsChineseCandidate(L"中文A") &&
             !ziliu::core::IsChineseCandidate(L"中文1") &&
             !ziliu::core::IsChineseCandidate(L"中文。") &&
             !ziliu::core::IsChineseCandidate(L"😀"),
         "Chinese candidate detection should accept only Han text");

  Type(*engine, L"nihao");
  Expect(engine->Backspace(), "backspace should consume an existing letter");
  Expect(engine->Snapshot().preedit == L"niha", "backspace should remove one letter");

  engine->Reset();
  Expect(!engine->Backspace(), "backspace should pass through on an empty composition");
  Expect(!engine->ProcessLetter(L'1'), "non-letters should pass through");
  Expect(engine->ProcessLetter(L'x') && engine->Snapshot().candidates.empty(),
         "Chinese-only filtering should be enabled by default");
  engine->SetChineseCandidatesOnly(false);
  Expect(engine->Snapshot().candidates.size() == 1 &&
             engine->Snapshot().candidates.front().text == L"x",
         "disabling Chinese-only filtering should restore non-Chinese candidates");
  engine->SetChineseCandidatesOnly(true);
  Expect(engine->Snapshot().candidates.empty(),
         "re-enabling Chinese-only filtering should immediately update candidates");
  engine->Reset();

  engine->SetChineseCandidatesOnly(false);
  for (std::size_t index = 0; index < ziliu::core::kMaximumPinyinLetters - 1; ++index) {
    Expect(engine->ProcessLetter(L'h'), "input below the pinyin limit should be consumed");
  }
  Expect(engine->Snapshot().preedit.size() == 63 && !engine->Snapshot().candidates.empty(),
         "63 pinyin letters should retain visible candidates");
  Expect(engine->ProcessLetter(L'h'), "the 64th pinyin letter should be consumed");
  Expect(engine->Snapshot().preedit.size() == 64 && engine->Snapshot().candidates.empty(),
         "the 64th pinyin letter should hide candidates");
  Expect(engine->ProcessLetter(L'h') && engine->Snapshot().preedit.size() == 64,
         "pinyin letters beyond the limit should be consumed without being appended");
  Expect(engine->Backspace() && engine->Snapshot().preedit.size() == 63 &&
             !engine->Snapshot().candidates.empty(),
         "backspace to 63 pinyin letters should restore candidates");
  engine->Reset();
  engine->SetChineseCandidatesOnly(true);

  ziliu::core::SessionHost host;
  const auto created = host.Handle({1, 0, ziliu::core::ipc::Command::kCreateSession, 0});
  Expect(created.session_id != 0 && host.session_count() == 1,
         "session host should create an isolated engine");
  const auto input = host.Handle(
      {2, created.session_id, ziliu::core::ipc::Command::kInputLetter, L'z'});
  Expect(input.consumed && input.snapshot.preedit == L"z",
         "session host should route input to its engine");
  const auto filter_disabled = host.Handle(
      {3, created.session_id, ziliu::core::ipc::Command::kSetChineseCandidatesOnly, 0});
  Expect(filter_disabled.consumed && filter_disabled.snapshot.candidates.size() == 1 &&
             filter_disabled.snapshot.candidates.front().text == L"z",
         "session host should apply Chinese-only filtering changes");
  const auto separator = host.Handle(
      {4, created.session_id, ziliu::core::ipc::Command::kInputSeparator, 0});
  Expect(separator.consumed && separator.snapshot.preedit == L"z'",
         "session host should route a manual pinyin separator");
  std::vector<std::byte> request_bytes;
  const ziliu::core::ipc::Request separator_request{
      5, created.session_id, ziliu::core::ipc::Command::kInputSeparator, 0};
  Expect(ziliu::core::ipc::EncodeRequest(separator_request, &request_bytes),
         "separator request should encode");
  ziliu::core::ipc::Request decoded_request;
  Expect(ziliu::core::ipc::DecodeRequest(request_bytes, &decoded_request) &&
             decoded_request.command == ziliu::core::ipc::Command::kInputSeparator,
         "separator request should survive the IPC round trip");

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
             defaults.chinese_candidates_only &&
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
      "chinese_candidates_only=false\n"
      "fuzzy_z_zh=true\n"
      "smart_numeric_punctuation=false\n"
      "theme_mode=dark\n"
      "candidate_page_mode=multi_line\n"
      "custom_candidate_colors=true\n"
      "preedit_color=#112233\n"
      "highlighted_candidate_color=#245678\n"
      "candidate_text_color=#334455\n"
      "candidate_background_color=#F0F1F2\n"
      "custom_candidate_fonts=true\n"
      "candidate_chinese_font_family=microsoft_yahei\n"
      "candidate_english_font_family=arial\n"
      "custom_candidate_font_size=true\n"
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
             !parsed_settings.chinese_candidates_only &&
             parsed_settings.fuzzy_z_zh && !parsed_settings.smart_numeric_punctuation &&
             parsed_settings.theme_mode == ziliu::core::ThemeMode::kDark &&
             parsed_settings.candidate_page_mode ==
                 ziliu::core::CandidatePageMode::kMultiLine &&
             parsed_settings.custom_candidate_colors &&
             parsed_settings.preedit_color == 0x112233 &&
             parsed_settings.highlighted_candidate_color == 0x245678 &&
             parsed_settings.candidate_text_color == 0x334455 &&
             parsed_settings.candidate_background_color == 0xF0F1F2 &&
             parsed_settings.custom_candidate_fonts &&
             parsed_settings.candidate_chinese_font_family == "Microsoft YaHei UI" &&
             parsed_settings.candidate_english_font_family == "Arial" &&
             parsed_settings.custom_candidate_font_size &&
             parsed_settings.candidate_font_size == 20 &&
             !parsed_settings.candidate_scale_with_text,
         "settings parser should preserve all supported choices");
  Expect(ziliu::core::ParseSettings(ziliu::core::SerializeSettings(parsed_settings)) ==
             parsed_settings,
         "settings should survive a deterministic serialization round trip");
  const auto custom_font_settings =
      ziliu::core::ParseSettings("candidate_chinese_font_family=霞鹜文楷\n"
                                 "candidate_english_font_family=IBM Plex Sans\n");
  Expect(custom_font_settings.candidate_chinese_font_family == "霞鹜文楷" &&
             custom_font_settings.candidate_english_font_family == "IBM Plex Sans" &&
             ziliu::core::ParseSettings(
                 ziliu::core::SerializeSettings(custom_font_settings)) == custom_font_settings,
         "settings should preserve installed font family names as UTF-8");
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
