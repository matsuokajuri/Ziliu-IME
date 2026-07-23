#include "ziliu/core/engine.h"

#include <algorithm>
#include <cwctype>
#include <unordered_map>
#include <utility>

namespace ziliu::core {
namespace {

using CandidateList = std::vector<Candidate>;

const std::unordered_map<std::wstring, CandidateList> kSeedCandidates = {
    {L"ni", {{L"你", L"ni", 1.0}, {L"呢", L"ni", 0.8}}},
    {L"nihao", {{L"你好", L"ni'hao", 1.0}, {L"拟好", L"ni'hao", 0.3}}},
    {L"ziliu", {{L"字流", L"zi'liu", 1.0}, {L"自流", L"zi'liu", 0.6}}},
};

class StubEngine final : public Engine {
 public:
  void Reset() override {
    preedit_.clear();
    candidates_.clear();
  }

  bool ProcessLetter(wchar_t letter) override {
    if (!std::iswalpha(letter)) {
      return false;
    }

    preedit_.push_back(static_cast<wchar_t>(std::towlower(letter)));
    RefreshCandidates();
    return true;
  }

  bool Backspace() override {
    if (preedit_.empty()) {
      return false;
    }

    preedit_.pop_back();
    RefreshCandidates();
    return true;
  }

  bool PageUp() override { return false; }

  bool PageDown() override { return false; }

  void SetCandidatePageSize(std::size_t page_size) override {
    static_cast<void>(page_size);
  }

  void SetTraditional(bool enabled) override { traditional_ = enabled; }

  SelectionResult Select(std::size_t candidate_index) override {
    if (candidate_index >= candidates_.size()) {
      return {};
    }

    std::wstring result = candidates_[candidate_index].text;
    Reset();
    return SelectionResult{true, std::move(result)};
  }

  [[nodiscard]] CompositionSnapshot Snapshot() const override {
    return CompositionSnapshot{preedit_, candidates_, 0};
  }

 private:
  void RefreshCandidates() {
    const auto found = kSeedCandidates.find(preedit_);
    if (found != kSeedCandidates.end()) {
      candidates_ = found->second;
      return;
    }

    candidates_.clear();
    if (!preedit_.empty()) {
      const Candidate raw_candidate{preedit_, L"原样输入", 0.0};
      if (IsChineseCandidate(raw_candidate.text)) {
        candidates_.push_back(raw_candidate);
      }
    }
  }

  std::wstring preedit_;
  CandidateList candidates_;
  bool traditional_ = false;
};

}  // namespace

std::unique_ptr<Engine> CreateStubEngine() { return std::make_unique<StubEngine>(); }

}  // namespace ziliu::core
