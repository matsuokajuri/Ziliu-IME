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
    pinyin_letter_count_ = 0;
  }

  bool ProcessLetter(wchar_t letter) override {
    if (!std::iswalpha(letter)) {
      return false;
    }
    if (pinyin_letter_count_ >= kMaximumPinyinLetters) {
      return true;
    }

    preedit_.push_back(static_cast<wchar_t>(std::towlower(letter)));
    ++pinyin_letter_count_;
    RefreshCandidates();
    return true;
  }

  bool ProcessSeparator() override {
    if (preedit_.empty() || preedit_.back() == L'\'') {
      return false;
    }
    preedit_.push_back(L'\'');
    RefreshCandidates();
    return true;
  }

  bool Backspace() override {
    if (preedit_.empty()) {
      return false;
    }

    if (preedit_.back() != L'\'') {
      --pinyin_letter_count_;
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

  void SetCandidateWindowPageCount(std::size_t page_count) override {
    static_cast<void>(page_count);
  }

  void SetTraditional(bool enabled) override { traditional_ = enabled; }

  void SetChineseCandidatesOnly(bool enabled) override {
    chinese_candidates_only_ = enabled;
    RefreshCandidates();
  }

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
    if (pinyin_letter_count_ >= kMaximumPinyinLetters) {
      candidates_.clear();
      return;
    }
    std::wstring lookup_key;
    lookup_key.reserve(preedit_.size());
    for (const wchar_t character : preedit_) {
      if (character != L'\'') {
        lookup_key.push_back(character);
      }
    }
    const auto found = kSeedCandidates.find(lookup_key);
    if (found != kSeedCandidates.end()) {
      candidates_ = found->second;
      return;
    }

    candidates_.clear();
    if (!preedit_.empty()) {
      const Candidate raw_candidate{preedit_, L"原样输入", 0.0};
      if (!chinese_candidates_only_ || IsChineseCandidate(raw_candidate.text)) {
        candidates_.push_back(raw_candidate);
      }
    }
  }

  std::wstring preedit_;
  CandidateList candidates_;
  bool traditional_ = false;
  bool chinese_candidates_only_ = true;
  std::size_t pinyin_letter_count_ = 0;
};

}  // namespace

std::unique_ptr<Engine> CreateStubEngine() { return std::make_unique<StubEngine>(); }

std::unique_ptr<Engine> CreateStubEngineForSession(bool restricted) {
  static_cast<void>(restricted);
  return CreateStubEngine();
}

}  // namespace ziliu::core
