#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace ziliu::core::scoring {

// No document-text retrieval occurs here. The caller owns privacy classification,
// context lifetime, source candidates and the native fallback/selection mapping.
enum class Privacy { kUnknown, kBlocked, kRestricted, kOrdinary };

struct Scope {
  std::uint64_t session = 0;
  std::uint64_t generation = 0;
  bool operator==(const Scope&) const = default;
};

struct Candidate {
  std::int64_t source_index = 0;
  std::string text;
  bool operator==(const Candidate&) const = default;
};

struct Request {
  Scope scope;
  std::uint64_t request_id = 0;
  Privacy privacy = Privacy::kUnknown;
  std::string prefix;
  std::vector<Candidate> candidates;
};

using Vocabulary = std::unordered_map<std::uint32_t, std::int64_t>;

// Five tensors for a trusted selected-causal-parent FP32 score graph. The graph
// computes full-vocabulary log-softmax and the mean over each candidate's targets.
struct PackedInputs {
  std::int64_t batch = 0;
  std::int64_t sequence = 0;
  std::int64_t target_count = 0;
  std::vector<std::int64_t> ids;
  std::vector<std::int64_t> parents;
  std::vector<std::int64_t> targets;
  std::vector<std::int64_t> score_indices;
  std::vector<std::uint8_t> score_valid;
  bool operator==(const PackedInputs&) const = default;
};

// Strict scalar decoding: rejects NUL, overlongs, surrogates and invalid UTF-8.
[[nodiscard]] std::vector<std::uint32_t> DecodeUtf8(const std::string& text,
                                                  std::size_t maximum_scalars,
                                                  bool require_nonempty = false);
void ValidateVocabulary(const Vocabulary& vocabulary);
[[nodiscard]] PackedInputs PackInputs(const Request& request, const Vocabulary& vocabulary);

enum class Status { kFallback, kScored };
struct Result {
  Scope scope;
  std::uint64_t request_id = 0;
  std::string manifest_sha256;
  Status status = Status::kFallback;
  std::string reason;
  std::vector<std::int64_t> source_indices;
  std::vector<std::int64_t> ranked_source_indices;
  // Empty on every fallback. A successful result covers the complete source pool.
  std::vector<float> scores;
};

// One owner thread, one immutable scope/vocabulary/model identity, no caching,
// persistence or network. Only monotonically increasing request IDs are accepted.
// A new context/privacy generation requires a new session. Malformed/stale input
// throws; caller must retain its native result on all exceptions and fallbacks.
class Session final {
 public:
  using Backend = std::function<std::vector<float>(const PackedInputs&)>;
  using ResourceCheck = std::function<void()>;
  using Now = std::function<double()>;

  Session(Scope scope, Vocabulary vocabulary, std::string manifest_sha256, Backend backend,
          ResourceCheck resource_check, std::uint64_t maximum_calls,
          double lifetime_seconds = 240, Now now = MonotonicSeconds);
  Session(const Session&) = delete;
  Session& operator=(const Session&) = delete;

  [[nodiscard]] Result Score(const Request& request);
  void Close();
  [[nodiscard]] std::uint64_t calls() const;

 private:
  static double MonotonicSeconds();
  void CheckOwner() const;

  const Scope scope_;
  const Vocabulary vocabulary_;
  const std::string manifest_sha256_;
  Backend backend_;
  ResourceCheck resource_check_;
  const Now now_;
  const std::thread::id owner_;
  const std::uint64_t maximum_calls_;
  const double deadline_;
  std::uint64_t calls_ = 0;
  std::uint64_t last_request_id_ = 0;
  bool busy_ = false;
  bool closed_ = false;
};

}  // namespace ziliu::core::scoring
