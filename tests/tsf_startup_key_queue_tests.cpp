#include "../src/tsf/src/startup_key_queue.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

int failures = 0;

void Expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    ++failures;
  }
}

}  // namespace

int main() {
  using ziliu::tsf::detail::StartupKeyQueue;
  using ziliu::tsf::detail::StartupKeyEffect;

  StartupKeyQueue queue;
  const std::uint64_t first_generation = queue.generation();
  constexpr std::array<std::uint32_t, 6> cold_start_keys{'N', 'I', 'H', 'A', 'O', 0x20};
  for (const std::uint32_t key : cold_start_keys) {
    const auto effect = key == 0x20 ? StartupKeyEffect::kCommit
                                    : StartupKeyEffect::kCompose;
    Expect(queue.Push(key, false, effect), "cold-start sequence fits in the bounded queue");
  }
  Expect(!queue.HasCompositionIntent(), "Space closes the queued composition intent");
  const auto keys = queue.Take(first_generation);
  Expect(keys.size() == 6, "all startup keys are replayed");
  if (keys.size() == 6) {
    Expect(keys[0].key == 'N' && keys[1].key == 'I' && keys[2].key == 'H' &&
               keys[3].key == 'A' && keys[4].key == 'O' && keys[5].key == 0x20,
           "startup replay preserves key order including Space");
  }
  Expect(queue.empty(), "successful replay drains the queue exactly once");
  Expect(queue.Take(first_generation).empty(), "drained keys cannot be replayed twice");

  Expect(queue.Push('N', false, StartupKeyEffect::kCompose), "a new context can queue a key");
  const std::uint64_t stale_generation = queue.generation();
  queue.Cancel();
  Expect(queue.empty(), "focus or privacy cancellation drops pending keys");
  Expect(queue.generation() != stale_generation, "cancellation advances the context generation");
  Expect(queue.Push('Z', false, StartupKeyEffect::kCompose),
         "a replacement generation can accept new input");
  Expect(queue.Take(stale_generation).empty(), "a stale async edit cannot replay into a new field");
  Expect(!queue.empty(), "a stale callback cannot cancel replacement-generation input");
  static_cast<void>(queue.Take(queue.generation()));

  Expect(queue.Push('A', true, StartupKeyEffect::kCompose),
         "modifier state is captured with the key");
  const auto shifted = queue.Take(queue.generation());
  Expect(shifted.size() == 1 && shifted[0].shifted,
         "replay uses the modifier state from the original key event");

  StartupKeyQueue bounded;
  for (std::size_t index = 0; index < StartupKeyQueue::kMaximumKeys; ++index) {
    Expect(bounded.Push('A', false, StartupKeyEffect::kCompose),
           "queue accepts keys up to its documented bound");
  }
  Expect(!bounded.Push('B', false, StartupKeyEffect::kCompose),
         "queue rejects unbounded startup input");

  StartupKeyQueue intent;
  Expect(intent.Push('N', false, StartupKeyEffect::kCompose), "intent test queues n");
  Expect(intent.Push(0x08, false, StartupKeyEffect::kBackspace),
         "intent test queues Backspace");
  Expect(!intent.HasCompositionIntent(), "n then Backspace leaves no queued preedit");
  Expect(intent.Push(0x20, false, StartupKeyEffect::kCommit), "intent test queues Space");
  Expect(!intent.HasCompositionIntent(), "Space without preedit does not invent candidates");
  Expect(intent.Push('N', false, StartupKeyEffect::kCompose), "intent test queues later n");
  Expect(intent.Push('I', false, StartupKeyEffect::kCompose), "intent test queues later i");
  Expect(intent.HasCompositionIntent(), "later letters start a fresh queued composition");

  StartupKeyQueue two_compositions;
  for (const std::uint32_t key : cold_start_keys) {
    const auto effect = key == 0x20 ? StartupKeyEffect::kCommit
                                    : StartupKeyEffect::kCompose;
    Expect(two_compositions.Push(key, false, effect), "first composition queues");
  }
  Expect(two_compositions.Push(0x20, false, StartupKeyEffect::kCommit),
         "literal Space remains ordered behind the first commit");
  Expect(!two_compositions.HasCompositionIntent(),
         "two Spaces do not leave a phantom composition");
  Expect(two_compositions.Push('N', false, StartupKeyEffect::kCompose),
         "second composition starts after literal Space");
  for (const std::uint32_t key : {'I', 'H', 'A', 'O'}) {
    Expect(two_compositions.Push(key, false, StartupKeyEffect::kCompose),
           "second composition preserves its remaining letters");
  }
  const auto first_batch = two_compositions.TakePrefix(two_compositions.generation(), 8);
  Expect(first_batch.size() == 8 && first_batch[5].key == 0x20 &&
             first_batch[6].key == 0x20 && first_batch[7].key == 'N',
         "bounded replay keeps the literal Space and next composition in order");
  Expect(two_compositions.HasCompositionIntent(),
         "recomputing state after a replay batch retains the remaining preedit");
  Expect(two_compositions.Push('N', false, StartupKeyEffect::kCompose),
         "a real key arriving between replay batches queues behind the suffix");
  const auto second_batch = two_compositions.Take(two_compositions.generation());
  Expect(second_batch.size() == 5 && second_batch[0].key == 'I' &&
             second_batch[3].key == 'O' && second_batch[4].key == 'N',
         "the next bounded replay preserves suffix-before-new-key ordering");

  if (failures != 0) {
    std::cerr << failures << " startup key queue test(s) failed\n";
    return 1;
  }
  std::cout << "startup key queue tests passed\n";
  return 0;
}
