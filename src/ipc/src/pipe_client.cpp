#include "ziliu/ipc/pipe_client.h"

#include <windows.h>

#include <array>
#include <cstddef>
#include <utility>
#include <vector>

namespace ziliu::ipc {
namespace {

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

}  // namespace

PipeClient::PipeClient(std::wstring pipe_name, std::uint32_t timeout_milliseconds)
    : pipe_name_(std::move(pipe_name)), timeout_milliseconds_(timeout_milliseconds) {}

std::optional<core::ipc::Response> PipeClient::Exchange(
    const core::ipc::Request& request) const {
  std::vector<std::byte> request_bytes;
  if (!core::ipc::EncodeRequest(request, &request_bytes) ||
      !WaitNamedPipeW(pipe_name_.c_str(), timeout_milliseconds_)) {
    return std::nullopt;
  }

  Handle pipe(CreateFileW(pipe_name_.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                          OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr));
  if (!pipe.valid()) {
    return std::nullopt;
  }
  DWORD read_mode = PIPE_READMODE_MESSAGE;
  if (!SetNamedPipeHandleState(pipe.get(), &read_mode, nullptr, nullptr)) {
    return std::nullopt;
  }

  Handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
  if (!event.valid()) {
    return std::nullopt;
  }
  OVERLAPPED operation{};
  operation.hEvent = event.get();
  std::array<std::byte, core::ipc::kMaximumMessageBytes> response_bytes{};
  DWORD bytes_read = 0;
  BOOL completed = TransactNamedPipe(pipe.get(), request_bytes.data(),
                                     static_cast<DWORD>(request_bytes.size()),
                                     response_bytes.data(), static_cast<DWORD>(response_bytes.size()),
                                     &bytes_read, &operation);
  if (!completed) {
    const DWORD error = GetLastError();
    if (error != ERROR_IO_PENDING) {
      return std::nullopt;
    }
    if (WaitForSingleObject(event.get(), timeout_milliseconds_) != WAIT_OBJECT_0) {
      CancelIoEx(pipe.get(), &operation);
      DWORD cancelled_bytes = 0;
      // An OVERLAPPED structure must outlive its operation. Named-pipe
      // cancellation completes promptly; drain it before stack unwinding.
      static_cast<void>(GetOverlappedResult(pipe.get(), &operation, &cancelled_bytes, TRUE));
      return std::nullopt;
    }
    if (!GetOverlappedResult(pipe.get(), &operation, &bytes_read, FALSE)) {
      return std::nullopt;
    }
  }

  core::ipc::Response response;
  if (!core::ipc::DecodeResponse(
          std::span<const std::byte>(response_bytes.data(), bytes_read), &response) ||
      response.request_id != request.request_id) {
    return std::nullopt;
  }
  return response;
}

}  // namespace ziliu::ipc
