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
};

class Engine {
 public:
  virtual ~Engine() = default;

  virtual void Reset() = 0;
  virtual bool ProcessLetter(wchar_t letter) = 0;
  virtual bool Backspace() = 0;
  virtual std::wstring Select(std::size_t candidate_index) = 0;
  [[nodiscard]] virtual CompositionSnapshot Snapshot() const = 0;
};

// Temporary deterministic engine used to validate the Windows shell before
// librime is connected. It deliberately contains no persistence or network IO.
[[nodiscard]] std::unique_ptr<Engine> CreateStubEngine();

}  // namespace ziliu::core

