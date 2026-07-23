#include "ziliu/core/engine.h"

namespace ziliu::core {

bool IsPureEnglishCandidate(std::wstring_view text) noexcept {
  bool has_letter = false;
  for (const wchar_t character : text) {
    if ((character >= L'A' && character <= L'Z') ||
        (character >= L'a' && character <= L'z')) {
      has_letter = true;
      continue;
    }
    if (character == L' ' || character == L'\t' || character == L'\'' ||
        character == L'-') {
      continue;
    }
    return false;
  }
  return has_letter;
}

}  // namespace ziliu::core
