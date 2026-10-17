#include "ziliu/core/scorer.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>
#include <set>
#include <stdexcept>
#include <utility>

namespace ziliu::core::scoring {
namespace {

void Require(bool condition, const char* reason) {
  if (!condition) throw std::runtime_error(reason);
}

bool IsDigest(const std::string& value) {
  return value.size() == 64 && std::all_of(value.begin(), value.end(), [](char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
  });
}

void ValidateRequest(const Request& request) {
  Require(request.request_id != 0, "invalid request identity");
  Require(request.candidates.size() >= 2 && request.candidates.size() <= 9,
          "invalid candidate count");
  (void)DecodeUtf8(request.prefix, 16384);
  std::int64_t last = -1;
  for (const auto& candidate : request.candidates) {
    Require(candidate.source_index >= 0 && candidate.source_index < 9 &&
                candidate.source_index > last && (last != -1 || candidate.source_index == 0),
            "invalid source identity or order");
    last = candidate.source_index;
    (void)DecodeUtf8(candidate.text, 63, true);
  }
}

}  // namespace

std::vector<std::uint32_t> DecodeUtf8(const std::string& text, std::size_t maximum_scalars,
                                    bool require_nonempty) {
  Require(maximum_scalars <= 65536 && text.size() <= maximum_scalars * 4, "UTF-8 byte bound");
  std::vector<std::uint32_t> result;
  for (std::size_t i = 0; i < text.size();) {
    auto scalar = static_cast<std::uint32_t>(static_cast<unsigned char>(text[i++]));
    unsigned continuation = 0;
    std::uint32_t minimum = 0;
    if (scalar < 0x80) {
      // ASCII already decoded.
    } else if (scalar >= 0xc2 && scalar <= 0xdf) {
      continuation = 1;
      minimum = 0x80;
      scalar &= 31;
    } else if (scalar >= 0xe0 && scalar <= 0xef) {
      continuation = 2;
      minimum = 0x800;
      scalar &= 15;
    } else if (scalar >= 0xf0 && scalar <= 0xf4) {
      continuation = 3;
      minimum = 0x10000;
      scalar &= 7;
    } else {
      throw std::runtime_error("invalid UTF-8 leading byte");
    }
    Require(i + continuation <= text.size(), "truncated UTF-8");
    for (unsigned j = 0; j < continuation; ++j) {
      const auto byte = static_cast<unsigned char>(text[i++]);
      Require((byte & 0xc0) == 0x80, "invalid UTF-8 continuation");
      scalar = (scalar << 6) | (byte & 63);
    }
    Require(scalar >= minimum && scalar <= 0x10ffff &&
                !(scalar >= 0xd800 && scalar <= 0xdfff) && scalar != 0,
            "invalid Unicode scalar");
    result.push_back(scalar);
    Require(result.size() <= maximum_scalars, "Unicode scalar bound");
  }
  Require(!require_nonempty || !result.empty(), "empty text");
  return result;
}

void ValidateVocabulary(const Vocabulary& vocabulary) {
  Require(!vocabulary.empty() && vocabulary.size() <= 8189, "invalid vocabulary size");
  std::set<std::int64_t> ids;
  for (const auto& [scalar, id] : vocabulary) {
    Require(scalar > 0 && scalar <= 0x10ffff && !(scalar >= 0xd800 && scalar <= 0xdfff),
            "invalid vocabulary scalar");
    Require(id >= 3 && id < 8192 && ids.insert(id).second, "invalid vocabulary token");
  }
}

PackedInputs PackInputs(const Request& request, const Vocabulary& vocabulary) {
  ValidateRequest(request);
  PackedInputs packed;
  packed.batch = static_cast<std::int64_t>(request.candidates.size());
  const auto context = DecodeUtf8(request.prefix, 16384);
  std::size_t maximum = 0;
  std::vector<std::vector<std::uint32_t>> tails;
  for (const auto& candidate : request.candidates) {
    tails.push_back(DecodeUtf8(candidate.text, 63, true));
    maximum = std::max(maximum, tails.back().size());
  }
  Require(context.size() + maximum <= 63, "context truncation forbidden");
  std::vector<std::int64_t> prefix{2};
  for (const auto scalar : context) {
    const auto found = vocabulary.find(scalar);
    prefix.push_back(found == vocabulary.end() ? 1 : found->second);
  }
  packed.sequence = static_cast<std::int64_t>(context.size() + maximum);
  std::int64_t offset = 0;
  for (std::size_t i = 0; i < tails.size(); ++i) {
    std::vector<std::int64_t> targets;
    for (const auto scalar : tails[i]) {
      const auto found = vocabulary.find(scalar);
      Require(found != vocabulary.end(), "whole-pool candidate OOV");
      targets.push_back(found->second);
    }
    auto sequence = prefix;
    sequence.insert(sequence.end(), targets.begin(), targets.end());
    sequence.pop_back();
    sequence.resize(static_cast<std::size_t>(packed.sequence), 0);
    packed.ids.insert(packed.ids.end(), sequence.begin(), sequence.end());
    for (std::size_t j = 0; j < targets.size(); ++j) {
      packed.parents.push_back(static_cast<std::int64_t>(i) * packed.sequence +
                               static_cast<std::int64_t>(prefix.size() - 1 + j));
      packed.targets.push_back(targets[j]);
    }
    for (std::size_t j = 0; j < 63; ++j) {
      packed.score_indices.push_back(j < targets.size() ? offset + static_cast<std::int64_t>(j) : 0);
      packed.score_valid.push_back(static_cast<std::uint8_t>(j < targets.size()));
    }
    offset += static_cast<std::int64_t>(targets.size());
  }
  packed.target_count = offset;
  return packed;
}

double Session::MonotonicSeconds() {
  return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

Session::Session(Scope scope, Vocabulary vocabulary, std::string manifest_sha256, Backend backend,
                 ResourceCheck resource_check, std::uint64_t maximum_calls, double lifetime_seconds,
                 Now now)
    : scope_(scope), vocabulary_(std::move(vocabulary)), manifest_sha256_(std::move(manifest_sha256)),
      backend_(std::move(backend)), resource_check_(std::move(resource_check)), now_(std::move(now)),
      owner_(std::this_thread::get_id()), maximum_calls_(maximum_calls),
      deadline_(now_ ? now_() + lifetime_seconds : 0) {
  Require(scope_.session != 0 && scope_.generation != 0 && IsDigest(manifest_sha256_) && backend_ &&
              resource_check_ && now_ && maximum_calls_ > 0 && maximum_calls_ <= 100000 &&
              std::isfinite(lifetime_seconds) && lifetime_seconds > 0 && lifetime_seconds <= 300 &&
              std::isfinite(deadline_),
          "invalid bounded scorer session");
  ValidateVocabulary(vocabulary_);
}

void Session::CheckOwner() const {
  Require(std::this_thread::get_id() == owner_, "scorer owner thread required");
}

std::uint64_t Session::calls() const {
  CheckOwner();
  return calls_;
}

void Session::Close() {
  CheckOwner();
  Require(!busy_, "cannot close active scorer");
  closed_ = true;
  backend_ = nullptr;
  resource_check_ = nullptr;
}

Result Session::Score(const Request& input) {
  CheckOwner();
  Require(!busy_ && !closed_, "scorer unavailable");
  busy_ = true;
  struct Reset {
    bool& busy;
    ~Reset() { busy = false; }
  } reset{busy_};
  Require(input.scope == scope_ && input.request_id > last_request_id_, "stale scorer request");
  ValidateRequest(input);  // Bound all caller-owned storage before copying it.
  const Request request = input;
  last_request_id_ = request.request_id;
  Result result;
  result.scope = scope_;
  result.request_id = request.request_id;
  result.manifest_sha256 = manifest_sha256_;
  for (const auto& candidate : request.candidates) result.source_indices.push_back(candidate.source_index);
  result.ranked_source_indices = result.source_indices;
  const auto fallback = [&](const char* reason) {
    result.reason = reason;
    return result;
  };
  if (request.privacy != Privacy::kOrdinary) return fallback("PRIVACY_NOT_ORDINARY");
  if (calls_ >= maximum_calls_) return fallback("CALL_BUDGET");
  const double started = now_();
  if (!std::isfinite(started) || started >= deadline_) return fallback("DEADLINE");
  std::size_t maximum = 0;
  for (const auto& candidate : request.candidates) {
    const auto scalars = DecodeUtf8(candidate.text, 63, true);
    maximum = std::max(maximum, scalars.size());
    for (const auto scalar : scalars) {
      if (!vocabulary_.contains(scalar)) return fallback("WHOLE_POOL_CANDIDATE_OOV");
    }
  }
  if (DecodeUtf8(request.prefix, 16384).size() + maximum > 63)
    return fallback("CONTEXT_TRUNCATION_FORBIDDEN");
  try {
    auto packed = PackInputs(request, vocabulary_);
    const auto original = packed;
    resource_check_();
    ++calls_;
    auto scores = backend_(packed);
    resource_check_();
    if (packed != original) return fallback("BACKEND_MUTATED_INPUT");
    const double finished = now_();
    if (!std::isfinite(finished) || finished < started || finished >= deadline_)
      return fallback("DEADLINE");
    if (scores.size() != result.source_indices.size() ||
        std::any_of(scores.begin(), scores.end(), [](float score) { return !std::isfinite(score); }))
      return fallback("INVALID_SCORE_OUTPUT");
    std::map<std::pair<std::string, std::vector<std::int64_t>>, float> first;
    for (std::size_t i = 0; i < scores.size(); ++i) {
      std::vector<std::int64_t> tokens;
      for (const auto scalar : DecodeUtf8(request.candidates[i].text, 63, true))
        tokens.push_back(vocabulary_.at(scalar));
      const auto [it, inserted] = first.emplace(
          std::make_pair(request.candidates[i].text, std::move(tokens)), scores[i]);
      (void)inserted;
      scores[i] = it->second;
    }
    std::vector<std::size_t> order(scores.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](auto a, auto b) { return scores[a] > scores[b]; });
    std::vector<std::int64_t> ranked;
    ranked.reserve(order.size());
    for (const auto i : order) ranked.push_back(result.source_indices[i]);
    result.scores = std::move(scores);
    result.ranked_source_indices = std::move(ranked);
    result.status = Status::kScored;
    return result;
  } catch (const std::exception&) {
    return fallback("BACKEND_OR_RESOURCE_ERROR");
  }
}

}  // namespace ziliu::core::scoring
