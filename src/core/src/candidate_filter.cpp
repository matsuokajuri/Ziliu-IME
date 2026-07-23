#include "ziliu/core/engine.h"

#include <cstdint>

namespace ziliu::core {
namespace {

bool IsHanCodePoint(std::uint32_t code_point) noexcept {
  return code_point == 0x3007 ||
         (code_point >= 0x3400 && code_point <= 0x4DBF) ||
         (code_point >= 0x4E00 && code_point <= 0x9FFF) ||
         (code_point >= 0xF900 && code_point <= 0xFAFF) ||
         (code_point >= 0x20000 && code_point <= 0x2FA1F) ||
         (code_point >= 0x30000 && code_point <= 0x3347F);
}

}  // namespace

bool IsChineseCandidate(std::wstring_view text) noexcept {
  if (text.empty()) {
    return false;
  }
  for (std::size_t index = 0; index < text.size();) {
    std::uint32_t code_point = static_cast<std::uint32_t>(text[index++]);
    if constexpr (sizeof(wchar_t) == 2) {
      if (code_point >= 0xD800 && code_point <= 0xDBFF) {
        if (index >= text.size()) {
          return false;
        }
        const std::uint32_t low_surrogate =
            static_cast<std::uint32_t>(text[index++]);
        if (low_surrogate < 0xDC00 || low_surrogate > 0xDFFF) {
          return false;
        }
        code_point =
            0x10000 + ((code_point - 0xD800) << 10) + (low_surrogate - 0xDC00);
      } else if (code_point >= 0xDC00 && code_point <= 0xDFFF) {
        return false;
      }
    }
    if (!IsHanCodePoint(code_point)) {
      return false;
    }
  }
  return true;
}

std::size_t UnicodeCodePointCount(std::wstring_view text) noexcept {
  std::size_t count = 0;
  for (std::size_t index = 0; index < text.size(); ++index) {
    const std::uint32_t code_point = static_cast<std::uint32_t>(text[index]);
    if constexpr (sizeof(wchar_t) == 2) {
      if (code_point >= 0xD800 && code_point <= 0xDBFF && index + 1 < text.size()) {
        const std::uint32_t low_surrogate = static_cast<std::uint32_t>(text[index + 1]);
        if (low_surrogate >= 0xDC00 && low_surrogate <= 0xDFFF) {
          ++index;
        }
      }
    }
    ++count;
  }
  return count;
}

}  // namespace ziliu::core
