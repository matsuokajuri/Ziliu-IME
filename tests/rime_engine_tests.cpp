#include "ziliu/broker/rime_engine.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

void Expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

std::vector<std::pair<std::filesystem::path, std::filesystem::file_time_type>>
PrepareUnchangedOverlays(const std::filesystem::path& executable_path) {
  std::vector<std::pair<std::filesystem::path, std::filesystem::file_time_type>> timestamps;
  char* user_data_value = nullptr;
  std::size_t user_data_length = 0;
  if (_dupenv_s(&user_data_value, &user_data_length, "ZILIU_RIME_USER_DATA_DIR") != 0 ||
      user_data_value == nullptr) {
    std::free(user_data_value);
    return timestamps;
  }

  const std::filesystem::path user_data_path(user_data_value);
  std::free(user_data_value);
  const auto shared_data_path = executable_path.parent_path() / "data" / "rime";
  std::error_code file_error;
  std::filesystem::create_directories(user_data_path, file_error);
  Expect(!file_error, "Rime test user data directory should be available");
  const auto sentinel = std::filesystem::file_time_type::clock::now() - std::chrono::hours(48);
  for (const auto* overlay : {"default.custom.yaml", "rime_ice.custom.yaml"}) {
    const auto source = shared_data_path / overlay;
    if (!std::filesystem::is_regular_file(source, file_error) || file_error) {
      timestamps.clear();
      return timestamps;
    }
    const auto destination = user_data_path / overlay;
    file_error.clear();
    std::filesystem::copy_file(source, destination,
                               std::filesystem::copy_options::overwrite_existing, file_error);
    Expect(!file_error, "Rime test overlay should be prepared");
    std::filesystem::last_write_time(destination, sentinel, file_error);
    Expect(!file_error, "Rime test overlay timestamp should be adjustable");
    timestamps.emplace_back(destination, sentinel);
  }
  return timestamps;
}

}  // namespace

int main(int argument_count, char* arguments[]) {
  Expect(argument_count > 0 && arguments[0] != nullptr,
         "Rime test executable path should be available");
  const auto overlay_timestamps =
      PrepareUnchangedOverlays(std::filesystem::absolute(arguments[0]));
  auto engine = ziliu::broker::CreateEngine();
  for (const auto& [overlay, expected_timestamp] : overlay_timestamps) {
    std::error_code file_error;
    const auto actual_timestamp = std::filesystem::last_write_time(overlay, file_error);
    Expect(!file_error && actual_timestamp == expected_timestamp,
           "An unchanged Rime overlay should not be rewritten during cold start");
  }
  engine->SetCandidatePageSize(7);

  for (const wchar_t letter : std::wstring_view(L"nihao")) {
    Expect(engine->ProcessLetter(letter), "Rime should consume recovery acceptance input");
  }
  const auto recovery_snapshot = engine->Snapshot();
  Expect(recovery_snapshot.preedit == L"ni'hao",
         "the recovery build must preserve automatic pinyin separators");
  Expect(recovery_snapshot.candidates.size() == 7 &&
             recovery_snapshot.candidates.front().text == L"你好",
         "the recovery build must use real Rime candidates, not the two-word Stub");
  engine->Reset();

  for (const wchar_t letter : std::wstring_view(L"shi")) {
    Expect(engine->ProcessLetter(letter), "Rime should consume a paging test letter");
  }
  const auto first_page = engine->Snapshot();
  Expect(first_page.candidates.size() == 7, "Rime should honor the configured candidate page size");
  Expect(!first_page.has_previous_page && first_page.has_next_page,
         "the first Rime page should expose only a next-page affordance");
  const auto candidate_texts = [](const auto& snapshot) {
    std::vector<std::wstring> texts;
    texts.reserve(snapshot.candidates.size());
    for (const auto& candidate : snapshot.candidates) {
      texts.push_back(candidate.text);
    }
    return texts;
  };
  const auto contains_non_chinese = [](const auto& snapshot) {
    return std::ranges::any_of(snapshot.candidates, [](const auto& candidate) {
      return !ziliu::core::IsChineseCandidate(candidate.text);
    });
  };
  Expect(!contains_non_chinese(first_page),
         "the first visible page should contain only Chinese candidates");
  Expect(engine->PageDown(), "Rime should consume PageDown when more candidates exist");
  const auto second_page = engine->Snapshot();
  Expect(!second_page.candidates.empty(), "PageDown should retain candidate results");
  Expect(second_page.has_previous_page,
         "the second Rime page should expose a previous-page affordance");
  Expect(!contains_non_chinese(second_page),
         "later visible pages should contain only Chinese candidates");
  Expect(candidate_texts(second_page) != candidate_texts(first_page),
         "PageDown should advance to a different candidate page");
  Expect(engine->PageUp(), "Rime should consume PageUp on the second page");
  const auto returned_first_page = engine->Snapshot();
  Expect(candidate_texts(returned_first_page) == candidate_texts(first_page),
         "PageUp should return to the first candidate page");
  Expect(!returned_first_page.has_previous_page && returned_first_page.has_next_page,
         "returning to the first page should restore the pager affordances");
  engine->SetCandidateWindowPageCount(5);
  const auto expanded_first_page = engine->Snapshot();
  Expect(expanded_first_page.candidates.size() > 14 &&
             expanded_first_page.highlighted_index == 0,
         "multi-line paging should return several pages with the first row active");
  Expect(engine->PageDown(), "multi-line paging should advance to the second row");
  const auto expanded_second_page = engine->Snapshot();
  Expect(expanded_second_page.candidates == expanded_first_page.candidates &&
             expanded_second_page.highlighted_index == 7,
         "paging inside a five-row window should retain the group and move the active row");
  engine->SetCandidateWindowPageCount(1);
  engine->Reset();

  for (const wchar_t letter : std::wstring_view(L"no")) {
    Expect(engine->ProcessLetter(letter), "Rime should consume adjacent-key typo input");
  }
  bool found_corrected_candidate = false;
  for (int page = 0; page < 16 && !found_corrected_candidate; ++page) {
    const auto correction_page = engine->Snapshot();
    found_corrected_candidate =
        std::ranges::any_of(correction_page.candidates, [](const auto& candidate) {
          return candidate.text == L"你";
        });
    if (!found_corrected_candidate && !engine->PageDown()) {
      break;
    }
  }
  Expect(found_corrected_candidate,
         "librime adjacent-key correction should offer 你 when i is mistyped as o");
  engine->Reset();

  const auto type_spelling = [&engine](std::wstring_view spelling) {
    for (const wchar_t letter : spelling) {
      Expect(engine->ProcessLetter(letter), "Rime should consume learning test input");
    }
  };
  type_spelling(L"shi");
  const auto learning_baseline = engine->Snapshot();
  Expect(learning_baseline.candidates.size() >= 4,
         "Rime should expose enough candidates for a learning test");
  const std::size_t learned_candidate_index =
      (std::min)(std::size_t{5}, learning_baseline.candidates.size() - 1);
  const std::wstring learned_candidate =
      learning_baseline.candidates[learned_candidate_index].text;
  engine->Reset();
  for (int repetition = 0; repetition < 8; ++repetition) {
    type_spelling(L"shi");
    const auto learning_page = engine->Snapshot();
    const auto candidate =
        std::ranges::find_if(learning_page.candidates, [&learned_candidate](const auto& item) {
          return item.text == learned_candidate;
        });
    Expect(candidate != learning_page.candidates.end(),
           "the selected learning candidate should remain available");
    const auto candidate_index =
        static_cast<std::size_t>(candidate - learning_page.candidates.begin());
    Expect(engine->Select(candidate_index).consumed,
           "Rime should consume a user-learning selection");
  }
  type_spelling(L"shi");
  const auto learned_snapshot = engine->Snapshot();
  const auto learned_position =
      std::ranges::find_if(learned_snapshot.candidates, [&learned_candidate](const auto& item) {
        return item.text == learned_candidate;
      });
  Expect(learned_position != learned_snapshot.candidates.end(),
         "the learned candidate should remain visible");
  Expect(static_cast<std::size_t>(learned_position - learned_snapshot.candidates.begin()) <
             learned_candidate_index,
         "repeated selections should promote a learned candidate");
  engine->Reset();

  for (const wchar_t letter : std::wstring_view(L"xi")) {
    Expect(engine->ProcessLetter(letter), "Rime should consume manual-split input");
  }
  Expect(engine->ProcessSeparator(), "Rime should consume a manual pinyin separator");
  for (const wchar_t letter : std::wstring_view(L"an")) {
    Expect(engine->ProcessLetter(letter), "Rime should consume input after a manual separator");
  }
  const auto manual_split = engine->Snapshot();
  Expect(manual_split.preedit == L"xi'an",
         "manual pinyin splitting should be displayed with an apostrophe");
  Expect(std::ranges::any_of(manual_split.candidates,
                            [](const auto& candidate) { return candidate.text == L"西安"; }),
         "manual pinyin splitting should retain matching candidates");
  engine->Reset();

  for (const wchar_t letter : std::wstring_view(L"zhongguo")) {
    Expect(engine->ProcessLetter(letter), "Rime should consume a pinyin letter");
  }

  const auto snapshot = engine->Snapshot();
  Expect(snapshot.preedit == L"zhong'guo",
         "automatic pinyin splitting should be displayed with an apostrophe");
  const auto china = std::ranges::find_if(snapshot.candidates, [](const auto& candidate) {
    return candidate.text == L"中国";
  });
  if (china == snapshot.candidates.end()) {
    if (snapshot.candidates.empty() ||
        (snapshot.candidates.size() == 1 && snapshot.candidates.front().text == L"zhongguo")) {
      std::cout << "SKIPPED: verified rime.dll is not staged\n";
      return 77;
    }
    Expect(false, "Rime Ice should offer 中国 for zhongguo");
  }

  const auto candidate_index = static_cast<std::size_t>(china - snapshot.candidates.begin());
  Expect(engine->Select(candidate_index) == ziliu::core::SelectionResult{true, L"中国"},
         "Rime should commit the selected candidate");
  Expect(engine->Snapshot().empty(), "Rime commit should clear the composition");

  for (const wchar_t letter : std::wstring_view(L"zhongguo")) {
    Expect(engine->ProcessLetter(letter), "Rime should consume partial-selection input");
  }
  bool selected_first_character = false;
  for (int page = 0; page < 16 && !selected_first_character; ++page) {
    const auto selection_page = engine->Snapshot();
    const auto first_character =
        std::ranges::find_if(selection_page.candidates, [](const auto& candidate) {
          return candidate.text == L"中";
        });
    if (first_character != selection_page.candidates.end()) {
      const auto first_character_index =
          static_cast<std::size_t>(first_character - selection_page.candidates.begin());
      const auto partial_selection = engine->Select(first_character_index);
      Expect(partial_selection.consumed, "Rime should consume a single-character selection");
      Expect(partial_selection.commit.empty(),
             "Selecting the first character should not commit the remaining first choices");
      const auto remaining = engine->Snapshot();
      Expect(!remaining.empty(), "Partial selection should keep the remaining composition active");
      Expect(remaining.plain_text().starts_with(L"中"),
             "Partial selection should retain the chosen first character in preedit");
      selected_first_character = true;
      break;
    }
    if (!engine->PageDown()) {
      break;
    }
  }
  Expect(selected_first_character, "Rime Ice should offer 中 as a partial candidate for zhongguo");
  engine->Reset();

  for (const wchar_t repeated_letter : std::wstring_view(L"has")) {
    for (int index = 0; index < 63; ++index) {
      Expect(engine->ProcessLetter(repeated_letter),
             "Rime should consume long composition input for every pinyin initial");
    }
    const auto below_limit = engine->Snapshot();
    Expect(below_limit.plain_text().size() == 63,
           "A 63-letter composition should retain all typed input");
    Expect(!below_limit.candidates.empty(),
           "Candidates should remain visible below the 64-letter limit");
    Expect(ziliu::core::IsChineseCandidate(below_limit.candidates.front().text),
           "Automatic Rime segments should remain attached to a Chinese candidate");

    Expect(engine->ProcessLetter(repeated_letter), "Rime should consume the 64th pinyin letter");
    const auto at_limit = engine->Snapshot();
    Expect(at_limit.plain_text().size() == 64,
           "The 64th pinyin letter should remain in the preedit");
    Expect(at_limit.candidates.empty(),
           "Candidates should be hidden at the 64-letter limit");
    Expect(engine->ProcessLetter(repeated_letter),
           "Input beyond the limit should be consumed and rejected");
    Expect(engine->Snapshot().plain_text().size() == 64,
           "Input beyond the limit should not change the preedit");
    Expect(engine->Backspace(), "Backspace should reduce the 64-letter composition");
    const auto restored_below_limit = engine->Snapshot();
    Expect(restored_below_limit.plain_text().size() == 63,
           "Backspace should restore a 63-letter composition");
    Expect(!restored_below_limit.candidates.empty(),
           "Backspace should restore candidates below the limit");
    engine->Reset();
  }

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
             ziliu::core::SelectionResult{true, L"字流"},
         "Ziliu overlay should commit 字流");
  std::cout << "ziliu_rime_engine_tests: OK\n";
  return EXIT_SUCCESS;
}
