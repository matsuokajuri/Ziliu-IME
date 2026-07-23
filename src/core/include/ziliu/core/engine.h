#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace ziliu::core {

struct Candidate {
  std::wstring text;
  std::wstring annotation;
  double score = 0.0;

  bool operator==(const Candidate&) const = default;
};

struct CompositionSnapshot {
  std::wstring preedit;
  std::vector<Candidate> candidates;
  std::size_t highlighted_index = 0;

  [[nodiscard]] bool empty() const noexcept { return preedit.empty(); }
  [[nodiscard]] std::wstring plain_text() const {
    std::wstring result;
    result.reserve(preedit.size());
    for (const wchar_t character : preedit) {
      if (character != L'\'' && character != L' ' && character != L'\t' && character != L'\r' &&
          character != L'\n') {
        result.push_back(character);
      }
    }
    return result;
  }
};

struct SelectionResult {
  bool consumed = false;
  std::wstring commit;

  bool operator==(const SelectionResult&) const = default;
};

[[nodiscard]] bool IsChineseCandidate(std::wstring_view text) noexcept;

class Engine {
 public:
  virtual ~Engine() = default;

  virtual void Reset() = 0;
  virtual bool ProcessLetter(wchar_t letter) = 0;
  virtual bool ProcessSeparator() = 0;
  virtual bool Backspace() = 0;
  virtual bool PageUp() = 0;
  virtual bool PageDown() = 0;
  virtual void SetCandidatePageSize(std::size_t page_size) = 0;
  virtual void SetTraditional(bool enabled) = 0;
  virtual void SetChineseCandidatesOnly(bool enabled) = 0;
  virtual SelectionResult Select(std::size_t candidate_index) = 0;
  [[nodiscard]] virtual CompositionSnapshot Snapshot() const = 0;
};

// Temporary deterministic engine used to validate the Windows shell before
// librime is connected. It deliberately contains no persistence or network IO.
[[nodiscard]] std::unique_ptr<Engine> CreateStubEngine();

}  // namespace ziliu::core
