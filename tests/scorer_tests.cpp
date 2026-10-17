#include "ziliu/core/scorer.h"

#include <cstdlib>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <thread>

namespace s = ziliu::core::scoring;
namespace {
int checks = 0;
void Expect(bool condition, const char* message) {
  ++checks;
  if (!condition) throw std::runtime_error(message);
}
template <typename Function> void Rejects(Function function) {
  bool rejected = false;
  try { function(); } catch (const std::exception&) { rejected = true; }
  Expect(rejected, "invalid operation accepted");
}
const s::Vocabulary kVocabulary{{'a', 3}, {'b', 4}, {0x1f600, 5}};
const std::string kDigest(64, 'a');
s::Request Request(std::string prefix = "", std::string first = "a", std::string second = "b") {
  return {{1, 1}, 1, s::Privacy::kOrdinary, std::move(prefix), {{0, std::move(first)}, {3, std::move(second)}}};
}
s::Session::Backend Standard() {
  return [](const s::PackedInputs&) { return std::vector<float>{1, 2}; };
}
std::unique_ptr<s::Session> Session(s::Session::Backend backend = Standard(),
                                  std::uint64_t calls = 2, s::Session::ResourceCheck check = [] {},
                                  s::Session::Now now = [] { return 0.; }) {
  return std::make_unique<s::Session>(s::Scope{1, 1}, kVocabulary, kDigest, std::move(backend),
                                     std::move(check), calls, 1, std::move(now));
}
}

int main() {
  try {
    Expect(s::DecodeUtf8("a\xf0\x9f\x98\x80", 2).size() == 2, "scalar decoding");
    for (const auto& bad : {std::string("\0", 1), std::string("\xc0\x80"),
                            std::string("\xed\xa0\x80"), std::string("\xf4\x90\x80\x80"),
                            std::string("\xf0\x80"), std::string("\x80")})
      Rejects([&] { (void)s::DecodeUtf8(bad, 63); });
    Rejects([] { (void)s::DecodeUtf8("aa", 1); });
    Rejects([] { (void)s::DecodeUtf8("", 1, true); });
    s::ValidateVocabulary(kVocabulary);
    Rejects([] { s::ValidateVocabulary({{'a', 2}}); });
    Rejects([] { s::ValidateVocabulary({{'a', 3}, {'b', 3}}); });
    Rejects([] { s::ValidateVocabulary({{0xd800, 3}}); });
    Rejects([] { s::ValidateVocabulary({}); });

    const auto packed = s::PackInputs(Request("a", "a", "bb"), kVocabulary);
    Expect(packed.ids == std::vector<std::int64_t>{2, 3, 0, 2, 3, 4}, "causal ragged ids");
    Expect(packed.parents == std::vector<std::int64_t>{1, 4, 5} &&
               packed.targets == std::vector<std::int64_t>{3, 4, 4}, "selected parents and targets");
    Expect(packed.score_indices[0] == 0 && packed.score_indices[63] == 1 &&
               packed.score_indices[64] == 2 && packed.score_indices[65] == 0 &&
               packed.score_valid[0] == 1 && packed.score_valid[1] == 0 &&
               packed.score_valid[64] == 1 && packed.score_valid[65] == 0, "score masks");
    Expect(s::PackInputs(Request("z"), kVocabulary).ids ==
               std::vector<std::int64_t>{2, 1, 2, 1}, "context unknown token");
    auto maximum = Request();
    maximum.candidates.clear();
    for (std::int64_t i = 0; i < 9; ++i) maximum.candidates.push_back({i, std::string(63, 'a')});
    const auto large = s::PackInputs(maximum, kVocabulary);
    Expect(large.batch == 9 && large.sequence == 63 && large.target_count == 567 &&
               large.parents.back() == 566 && large.score_indices.back() == 566, "maximum pack");
    for (const auto& modifier : std::vector<std::function<void(s::Request&)>>{
             [](auto& r) { r.candidates[0].source_index = 1; },
             [](auto& r) { r.candidates[1].source_index = 0; },
             [](auto& r) { r.candidates[1].source_index = 9; },
             [](auto& r) { r.candidates[1].text.clear(); },
             [](auto& r) { r.candidates[1].text = std::string(64, 'a'); },
             [](auto& r) { r.candidates.pop_back(); },
             [](auto& r) { r.prefix = std::string(16385, 'a'); },
             [](auto& r) { r.request_id = 0; }}) {
      auto request = Request();
      modifier(request);
      Rejects([&] { (void)Session()->Score(request); });
    }
    auto out = Session()->Score(Request());
    Expect(out.status == s::Status::kScored && out.ranked_source_indices ==
               std::vector<std::int64_t>{3, 0} && out.source_indices ==
               std::vector<std::int64_t>{0, 3} && out.request_id == 1 &&
               out.scope == s::Scope{1, 1} && out.manifest_sha256 == kDigest, "scored identity");
    for (const auto privacy : {s::Privacy::kUnknown, s::Privacy::kRestricted, s::Privacy::kBlocked}) {
      auto request = Request();
      request.privacy = privacy;
      auto session = Session();
      out = session->Score(request);
      Expect(out.reason == "PRIVACY_NOT_ORDINARY" && session->calls() == 0 && out.scores.empty(),
             "privacy admission");
    }
    auto session = Session();
    auto request = Request();
    request.scope.generation = 2;
    Rejects([&] { (void)session->Score(request); });
    request = Request();
    (void)session->Score(request);
    Rejects([&] { (void)session->Score(request); });
    request.request_id = 2;
    (void)session->Score(request);
    request.request_id = 3;
    Expect(session->Score(request).reason == "CALL_BUDGET" && session->calls() == 2, "call budget");
    session->Close();
    Rejects([&] { (void)session->Score(request); });
    Expect(Session()->Score(Request("", "a", "z")).reason == "WHOLE_POOL_CANDIDATE_OOV",
           "whole pool OOV");
    Expect(Session()->Score(Request(std::string(63, 'a'))).reason == "CONTEXT_TRUNCATION_FORBIDDEN",
           "no context truncation");
    auto duplicate = Session([](const auto&) { return std::vector<float>{2, 100}; });
    out = duplicate->Score(Request("", "a", "a"));
    Expect(out.scores == std::vector<float>{2, 2} && out.ranked_source_indices ==
               std::vector<std::int64_t>{0, 3}, "duplicate canonicalization");
    auto tie = Session([](const auto&) { return std::vector<float>{1, 1}; });
    Expect(tie->Score(Request()).ranked_source_indices == std::vector<std::int64_t>{0, 3},
           "stable exact tie");
    auto near = Session([](const auto&) { return std::vector<float>{1, 1.00000011920928955078125f}; });
    Expect(near->Score(Request()).ranked_source_indices == std::vector<std::int64_t>{3, 0},
           "no epsilon ties");
    for (auto values : {std::vector<float>{1}, std::vector<float>{1, std::numeric_limits<float>::infinity()},
                        std::vector<float>{1, std::numeric_limits<float>::quiet_NaN()}}) {
      auto invalid = Session([values](const auto&) { return values; });
      out = invalid->Score(Request());
      Expect(out.reason == "INVALID_SCORE_OUTPUT" && out.scores.empty() &&
                 out.ranked_source_indices == out.source_indices, "invalid output fallback");
    }
    auto raw_nan_duplicate = Session([](const auto&) {
      return std::vector<float>{1, std::numeric_limits<float>::quiet_NaN()};
    });
    Expect(raw_nan_duplicate->Score(Request("", "a", "a")).reason == "INVALID_SCORE_OUTPUT",
           "raw invalid value before duplicate canonicalization");
    double now = 0;
    auto expired = Session(Standard(), 2, [] {}, [&] { return now; });
    now = 2;
    Expect(expired->Score(Request()).reason == "DEADLINE" && expired->calls() == 0,
           "deadline before inference");
    now = 0;
    auto late = Session([&](const auto&) { now = 2; return std::vector<float>{1, 2}; },
                        2, [] {}, [&] { return now; });
    Expect(late->Score(Request()).reason == "DEADLINE", "deadline after inference");
    auto failed = Session(Standard(), 2, [] { throw std::runtime_error("test"); });
    Expect(failed->Score(Request()).reason == "BACKEND_OR_RESOURCE_ERROR" && failed->calls() == 0,
           "resource rejection before call");
    int probe = 0;
    auto changed = Session(Standard(), 2, [&] { if (++probe == 2) throw std::runtime_error("test"); });
    Expect(changed->Score(Request()).reason == "BACKEND_OR_RESOURCE_ERROR", "post resource failure");
    auto mutated = Session([](const auto& data) {
      const_cast<s::PackedInputs&>(data).ids[0] = 3;
      return std::vector<float>{1, 2};
    });
    Expect(mutated->Score(Request()).reason == "BACKEND_MUTATED_INPUT", "backend mutation");
    auto owned = Session();
    bool rejected_thread = false;
    std::thread thread([&] {
      try { (void)owned->Score(Request()); } catch (const std::exception&) { rejected_thread = true; }
    });
    thread.join();
    Expect(rejected_thread && owned->calls() == 0, "owner thread");
    s::Session* pointer = nullptr;
    bool rejected_reentry = false;
    bool rejected_close = false;
    auto reentrant = Session([&](const auto&) {
      try { (void)pointer->Score(Request()); } catch (const std::exception&) { rejected_reentry = true; }
      try { pointer->Close(); } catch (const std::exception&) { rejected_close = true; }
      return std::vector<float>{1, 2};
    });
    pointer = reentrant.get();
    Expect(reentrant->Score(Request()).status == s::Status::kScored && rejected_reentry && rejected_close,
           "reentry and busy close");
    auto vocabulary = kVocabulary;
    s::Session copied({1, 1}, vocabulary, kDigest, Standard(), [] {}, 1, 1, [] { return 0.; });
    vocabulary.clear();
    Expect(copied.Score(Request()).status == s::Status::kScored, "owned vocabulary snapshot");
    std::cout << "scorer host checks passed: " << checks << '\n';
    return EXIT_SUCCESS;
  } catch (const std::exception& error) {
    std::cerr << "scorer host test failed: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
