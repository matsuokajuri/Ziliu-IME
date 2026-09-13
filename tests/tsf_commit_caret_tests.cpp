#include "../src/tsf/src/commit_caret.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

namespace {
void Expect(bool value, std::string_view message) {
  if (!value) {
    std::cerr << "FAILED: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

// A bounded range double; no input-method registration or host input is needed.
struct InsertedRange {
  LONG start = 0;
  LONG end = 2;
  HRESULT shift_result = S_OK;
  HRESULT collapse_result = S_OK;
  bool short_shift = false;
  bool collapsed = false;
  HRESULT ShiftEnd(TfEditCookie, LONG requested, LONG* shifted, const TF_HALTCOND*) {
    Expect(!collapsed, "exclude closing delimiter before collapsing the inserted range");
    if (FAILED(shift_result)) return shift_result;
    *shifted = short_shift ? 0 : std::max(requested, start - end);
    end += *shifted;
    return S_OK;
  }
  HRESULT Collapse(TfEditCookie, TfAnchor anchor) {
    Expect(anchor == TF_ANCHOR_END, "caret is the end of the trimmed insertion");
    if (FAILED(collapse_result)) return collapse_result;
    start = end;
    collapsed = true;
    return S_OK;
  }
};
}

int main() {
  using ziliu::tsf::detail::CollapseInsertedRange;
  std::size_t cases = 0;
  for (std::wstring_view pair : {L"()", L"{}", L"\"\"", L"<>", L"[]", L"''",
                                 L"（）", L"｛｝", L"“”", L"《》", L"【】", L"‘’"}) {
    for (LONG offset : {0L, 5L}) {
      InsertedRange range{offset, offset + static_cast<LONG>(pair.size())};
      Expect(CollapseInsertedRange(&range, 1, 1) == S_OK, "place paired-symbol caret");
      Expect(range.start == offset + 1 && range.end == range.start, "empty selection between delimiters");
      std::wstring document(static_cast<std::size_t>(offset), L'x');
      document += pair;
      const auto original = document;
      document.insert(static_cast<std::size_t>(range.start), L"中");
      Expect(document[static_cast<std::size_t>(offset)] == pair.front() &&
                 document[static_cast<std::size_t>(offset + 2)] == pair.back(), "next text stays inside pair");
      document = original;
      document.erase(static_cast<std::size_t>(range.start - 1), 1);
      Expect(document[static_cast<std::size_t>(offset)] == pair.back(), "backspace removes opening delimiter");
      ++cases;
    }
  }
  InsertedRange ordinary{5, 8};
  Expect(CollapseInsertedRange(&ordinary, 1, 0) == S_OK && ordinary.start == 8, "ordinary commit ends after text");
  InsertedRange failed_shift; failed_shift.shift_result = E_ACCESSDENIED;
  Expect(CollapseInsertedRange(&failed_shift, 1, 1) == E_ACCESSDENIED, "propagate failed movement");
  InsertedRange partial; partial.short_shift = true;
  Expect(CollapseInsertedRange(&partial, 1, 1) == E_FAIL && !partial.collapsed, "reject incomplete movement");
  InsertedRange failed_collapse; failed_collapse.collapse_result = E_ABORT;
  Expect(CollapseInsertedRange(&failed_collapse, 1, 1) == E_ABORT, "propagate failed collapse");
  std::cout << "Paired caret range cases: " << cases << " PASS; contract-level test, not application input automation\n";
}
