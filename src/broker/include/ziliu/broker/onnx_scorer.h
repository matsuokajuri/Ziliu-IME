#pragma once

#include "ziliu/core/scorer.h"

#include <filesystem>
#include <memory>
#include <string>

namespace ziliu::broker {

// expected_manifest_sha256 must come from an independent trusted installation /
// approval channel, never from the adjacent manifest or untrusted IPC request.
// Paths and content are private runtime configuration, not compiled model assets.
struct TrustedScorerAssets {
  std::filesystem::path manifest_path;
  std::string expected_manifest_sha256;
};

// Optional worker-only factory. Throws before loading if trust, schema, resources
// or runtime identity cannot be established. The caller retains native results.
// Do not call in ZiliuTIP or the ambient Broker: an owned, externally bounded
// worker is required. No route, process launcher or asset download is provided.
[[nodiscard]] std::unique_ptr<core::scoring::Session> CreateOnnxScorer(
    const TrustedScorerAssets& assets, core::scoring::Scope scope, std::uint64_t maximum_calls,
    double lifetime_seconds = 240);

}  // namespace ziliu::broker
