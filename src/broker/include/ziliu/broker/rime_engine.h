#pragma once

#include "ziliu/core/engine.h"

#include <memory>

namespace ziliu::broker {

// Starts the Rime runtime as soon as the detached Broker process launches,
// before the first user key can become responsible for cold-start work.
void WarmUpEngineRuntime();

// Loads rime.dll beside ZiliuBroker.exe. A missing or incompatible runtime is
// a supported development state and falls back to the deterministic engine.
[[nodiscard]] std::unique_ptr<core::Engine> CreateEngine();

}  // namespace ziliu::broker
