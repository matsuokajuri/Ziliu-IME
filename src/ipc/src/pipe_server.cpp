#include "ziliu/ipc/pipe_server.h"

#include <windows.h>

#pragma warning(push)
// Windows SDK 10.0.28000 sddl.h contains trailing tokens on a preprocessor line.
#pragma warning(disable : 4067)
#include <sddl.h>
#pragma warning(pop)

#include <array>
#include <cstddef>
#include <exception>
#include <utility>
#include <vector>

namespace ziliu::ipc {
namespace {

class LocalMemory final {
 public:
  LocalMemory() = default;
  ~LocalMemory() {
    if (value_ != nullptr) {
      LocalFree(value_);
    }
  }
  LocalMemory(const LocalMemory&) = delete;
  LocalMemory& operator=(const LocalMemory&) = delete;
  [[nodiscard]] void** address() noexcept { return &value_; }
  [[nodiscard]] void* get() const noexcept { return value_; }

 private:
  void* value_ = nullptr;
};

class Handle final {
 public:
  explicit Handle(HANDLE value = INVALID_HANDLE_VALUE) : value_(value) {}
  ~Handle() {
    if (valid()) {
      CloseHandle(value_);
    }
  }
  Handle(const Handle&) = delete;
  Handle& operator=(const Handle&) = delete;
  [[nodiscard]] bool valid() const noexcept {
    return value_ != nullptr && value_ != INVALID_HANDLE_VALUE;
  }
  [[nodiscard]] HANDLE get() const noexcept { return value_; }

 private:
  HANDLE value_;
};

class PipeSecurity final {
 public:
  [[nodiscard]] bool Initialize() {
    HANDLE token_value = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token_value)) {
      return false;
    }
    Handle token(token_value);

    DWORD size = 0;
    GetTokenInformation(token.get(), TokenUser, nullptr, 0, &size);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || size == 0) {
      return false;
    }
    std::vector<std::byte> token_user_bytes(size);
    if (!GetTokenInformation(token.get(), TokenUser, token_user_bytes.data(), size, &size)) {
      return false;
    }
    const auto* token_user = reinterpret_cast<const TOKEN_USER*>(token_user_bytes.data());

    LocalMemory sid_text;
    if (!ConvertSidToStringSidW(token_user->User.Sid,
                                reinterpret_cast<wchar_t**>(sid_text.address()))) {
      return false;
    }
    const auto* sid = static_cast<const wchar_t*>(sid_text.get());
    // SearchHost and other AppContainer text clients need the package SID as
    // well as a low mandatory label. Requests are still authenticated below.
    const std::wstring sddl = L"D:P(A;;GA;;;SY)(A;;GRGW;;;S-1-15-2-1)(A;;GA;;;" +
                              std::wstring(sid) + L")S:(ML;;NW;;;LW)";
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            sddl.c_str(), SDDL_REVISION_1, descriptor_.address(), nullptr)) {
      return false;
    }
    attributes_.nLength = sizeof(attributes_);
    attributes_.lpSecurityDescriptor = descriptor_.get();
    attributes_.bInheritHandle = FALSE;
    return true;
  }

  [[nodiscard]] SECURITY_ATTRIBUTES* attributes() noexcept { return &attributes_; }

 private:
  SECURITY_ATTRIBUTES attributes_{};
  LocalMemory descriptor_;
};

bool SameUserAndSession(HANDLE pipe) {
  HANDLE server_value = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &server_value)) {
    return false;
  }
  Handle server(server_value);
  if (!ImpersonateNamedPipeClient(pipe)) {
    return false;
  }
  HANDLE client_value = nullptr;
  const BOOL opened = OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &client_value);
  // Never execute the engine or continue serving while impersonating a client.
  if (!RevertToSelf()) {
    std::terminate();
  }
  Handle client(client_value);
  if (!opened) {
    return false;
  }
  DWORD server_session = 0;
  DWORD client_session = 0;
  DWORD returned = 0;
  if (!GetTokenInformation(server.get(), TokenSessionId, &server_session,
                           sizeof(server_session), &returned) ||
      !GetTokenInformation(client.get(), TokenSessionId, &client_session,
                           sizeof(client_session), &returned) ||
      server_session != client_session) {
    return false;
  }
  DWORD server_size = 0;
  DWORD client_size = 0;
  GetTokenInformation(server.get(), TokenUser, nullptr, 0, &server_size);
  GetTokenInformation(client.get(), TokenUser, nullptr, 0, &client_size);
  if (server_size == 0 || client_size == 0) {
    return false;
  }
  std::vector<std::byte> server_user(server_size);
  std::vector<std::byte> client_user(client_size);
  if (!GetTokenInformation(server.get(), TokenUser, server_user.data(), server_size, &returned) ||
      !GetTokenInformation(client.get(), TokenUser, client_user.data(), client_size, &returned)) {
    return false;
  }
  return EqualSid(reinterpret_cast<TOKEN_USER*>(server_user.data())->User.Sid,
                  reinterpret_cast<TOKEN_USER*>(client_user.data())->User.Sid) != FALSE;
}

}  // namespace

PipeServer::PipeServer(std::wstring pipe_name, core::SessionHost::EngineFactory engine_factory)
    : pipe_name_(std::move(pipe_name)), session_host_(std::move(engine_factory)) {}

int PipeServer::Run() {
  PipeSecurity security;
  if (!security.Initialize()) {
    return 1;
  }

  Handle pipe(CreateNamedPipeW(
      pipe_name_.c_str(), PIPE_ACCESS_DUPLEX,
      PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1,
      static_cast<DWORD>(core::ipc::kMaximumMessageBytes),
      static_cast<DWORD>(core::ipc::kMaximumMessageBytes), 0, security.attributes()));
  if (!pipe.valid()) {
    return 2;
  }

  while (!stopping_.load()) {
    const BOOL connected = ConnectNamedPipe(pipe.get(), nullptr);
    if (!connected && GetLastError() != ERROR_PIPE_CONNECTED) {
      if (stopping_.load()) {
        break;
      }
      DisconnectNamedPipe(pipe.get());
      continue;
    }
    if (stopping_.load()) {
      break;
    }
    static_cast<void>(ServeClient(pipe.get()));
    DisconnectNamedPipe(pipe.get());
  }
  return 0;
}

void PipeServer::Stop() {
  stopping_.store(true);
  Handle wake_pipe(CreateFileW(pipe_name_.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                               OPEN_EXISTING, 0, nullptr));
}

bool PipeServer::ServeClient(void* pipe_handle) {
  const HANDLE pipe = static_cast<HANDLE>(pipe_handle);
  std::array<std::byte, core::ipc::kMaximumMessageBytes> request_bytes{};
  DWORD bytes_read = 0;
  core::ipc::Request request;
  core::ipc::Response response;
  std::uint16_t protocol_version = core::ipc::kProtocolVersion;
  if (!ReadFile(pipe, request_bytes.data(), static_cast<DWORD>(request_bytes.size()), &bytes_read,
                nullptr) ||
      !core::ipc::DecodeRequest(
          std::span<const std::byte>(request_bytes.data(), bytes_read), &request,
          &protocol_version)) {
    response.status = core::ipc::Status::kInvalidRequest;
  } else {
    if (!SameUserAndSession(pipe)) {
      return false;
    }
    response = session_host_.Handle(request);
  }

  std::vector<std::byte> response_bytes;
  if (!core::ipc::EncodeResponse(response, protocol_version, &response_bytes)) {
    return false;
  }
  DWORD bytes_written = 0;
  return WriteFile(pipe, response_bytes.data(), static_cast<DWORD>(response_bytes.size()),
                   &bytes_written, nullptr) != FALSE &&
         bytes_written == response_bytes.size();
}

}  // namespace ziliu::ipc
