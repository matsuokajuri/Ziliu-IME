#pragma once

#include "ziliu/broker/onnx_scorer.h"

namespace ziliu::broker::detail {

struct VerifiedScorerAssets {
  std::string manifest_sha256;
  std::string graph;
  core::scoring::Vocabulary vocabulary;
  std::filesystem::path runtime_path;
  std::string runtime_sha256;
};

// Exact-byte manifest pin, then exact asset pins and strict format admission.
[[nodiscard]] VerifiedScorerAssets LoadScorerAssets(const TrustedScorerAssets& trusted);

}  // namespace ziliu::broker::detail
