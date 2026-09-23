#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ziliu::tsf::detail {

enum class StartupKeyEffect {
  kCompose,
  kBackspace,
  kCancel,
  kCommit,
  kNeutral,
};

struct StartupKey final {
  std::uint32_t key = 0;
  bool shifted = false;
  StartupKeyEffect effect = StartupKeyEffect::kNeutral;
};

class StartupKeyQueue final {
 public:
  static constexpr std::size_t kMaximumKeys = 2048;

  [[nodiscard]] bool Push(std::uint32_t key, bool shifted, StartupKeyEffect effect) {
    if (keys_.size() >= kMaximumKeys) {
      return false;
    }
    keys_.push_back({key, shifted, effect});
    ApplyEffect(effect);
    return true;
  }

  [[nodiscard]] bool empty() const noexcept { return keys_.empty(); }
  [[nodiscard]] std::size_t size() const noexcept { return keys_.size(); }
  [[nodiscard]] std::uint64_t generation() const noexcept { return generation_; }

  [[nodiscard]] bool HasCompositionIntent() const noexcept { return composition_depth_ != 0; }

  [[nodiscard]] std::vector<StartupKey> TakePrefix(std::uint64_t expected_generation,
                                                   std::size_t maximum) {
    if (expected_generation != generation_ || maximum == 0) {
      return {};
    }
    const std::size_t count = minimum(maximum, keys_.size());
    std::vector<StartupKey> result(keys_.begin(), keys_.begin() + count);
    keys_.erase(keys_.begin(), keys_.begin() + count);
    composition_depth_ = 0;
    for (const StartupKey& key : keys_) {
      ApplyEffect(key.effect);
    }
    return result;
  }

  [[nodiscard]] std::vector<StartupKey> Take(std::uint64_t expected_generation) {
    return TakePrefix(expected_generation, keys_.size());
  }

  void Cancel() noexcept {
    keys_.clear();
    composition_depth_ = 0;
    ++generation_;
    if (generation_ == 0) {
      generation_ = 1;
    }
  }

 private:
  static constexpr std::size_t minimum(std::size_t left, std::size_t right) noexcept {
    return left < right ? left : right;
  }

  void ApplyEffect(StartupKeyEffect effect) noexcept {
    switch (effect) {
      case StartupKeyEffect::kCompose:
        ++composition_depth_;
        break;
      case StartupKeyEffect::kBackspace:
        if (composition_depth_ != 0) {
          --composition_depth_;
        }
        break;
      case StartupKeyEffect::kCancel:
      case StartupKeyEffect::kCommit:
        composition_depth_ = 0;
        break;
      case StartupKeyEffect::kNeutral:
        break;
    }
  }

  std::vector<StartupKey> keys_;
  std::size_t composition_depth_ = 0;
  std::uint64_t generation_ = 1;
};

}  // namespace ziliu::tsf::detail
