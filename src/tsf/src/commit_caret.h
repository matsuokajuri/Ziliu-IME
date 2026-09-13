#pragma once

#include <windows.h>
#include <msctf.h>

#include <cstddef>
#include <limits>

namespace ziliu::tsf::detail {

// Keep the inserted range intact until the closing delimiter has been excluded.
// Moving back from an already collapsed range needlessly crosses region anchors.
template <typename Range>
HRESULT CollapseInsertedRange(Range* range, TfEditCookie cookie, std::size_t caret_back) {
  if (range == nullptr || caret_back > static_cast<std::size_t>(std::numeric_limits<LONG>::max())) {
    return E_INVALIDARG;
  }
  if (caret_back != 0) {
    const LONG requested = -static_cast<LONG>(caret_back);
    LONG shifted = 0;
    const HRESULT result = range->ShiftEnd(cookie, requested, &shifted, nullptr);
    if (FAILED(result)) {
      return result;
    }
    if (shifted != requested) {
      return E_FAIL;
    }
  }
  return range->Collapse(cookie, TF_ANCHOR_END);
}

}  // namespace ziliu::tsf::detail
