#include "ziliu/core/session_host.h"

#include <cwctype>
#include <utility>

namespace ziliu::core {

SessionHost::SessionHost(EngineFactory engine_factory)
    : engine_factory_(std::move(engine_factory)) {}

std::uint64_t SessionHost::CreateSession() {
  auto engine = engine_factory_ ? engine_factory_() : nullptr;
  if (engine == nullptr) {
    return 0;
  }
  while (next_session_id_ == 0 || sessions_.contains(next_session_id_)) {
    ++next_session_id_;
  }
  const std::uint64_t session_id = next_session_id_++;
  sessions_.emplace(session_id, std::move(engine));
  return session_id;
}

ipc::Response SessionHost::Handle(const ipc::Request& request) {
  ipc::Response response;
  response.request_id = request.request_id;
  response.session_id = request.session_id;

  if (request.command == ipc::Command::kPing) {
    return response;
  }
  if (request.command == ipc::Command::kCreateSession) {
    response.session_id = CreateSession();
    if (response.session_id == 0) {
      response.status = ipc::Status::kInternalError;
    }
    return response;
  }

  const auto found = sessions_.find(request.session_id);
  if (found == sessions_.end()) {
    response.status = ipc::Status::kSessionNotFound;
    return response;
  }
  Engine& engine = *found->second;

  switch (request.command) {
    case ipc::Command::kCloseSession:
      sessions_.erase(found);
      return response;
    case ipc::Command::kReset:
      engine.Reset();
      response.consumed = true;
      break;
    case ipc::Command::kInputLetter: {
      if (request.value > static_cast<std::uint32_t>(WCHAR_MAX)) {
        response.status = ipc::Status::kInvalidRequest;
        return response;
      }
      const auto letter = static_cast<wchar_t>(request.value);
      response.consumed = std::iswalpha(letter) != 0 && engine.ProcessLetter(letter);
      break;
    }
    case ipc::Command::kBackspace:
      response.consumed = engine.Backspace();
      break;
    case ipc::Command::kPageUp:
      response.consumed = engine.PageUp();
      break;
    case ipc::Command::kPageDown:
      response.consumed = engine.PageDown();
      break;
    case ipc::Command::kSetTraditional:
      engine.SetTraditional(request.value != 0);
      response.consumed = true;
      break;
    case ipc::Command::kSelectCandidate:
      response.commit = engine.Select(request.value);
      response.consumed = !response.commit.empty();
      break;
    case ipc::Command::kPing:
    case ipc::Command::kCreateSession:
      break;
  }
  response.snapshot = engine.Snapshot();
  return response;
}

}  // namespace ziliu::core
