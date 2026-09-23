#include "ziliu/core/engine.h"
#include "ziliu/core/ipc_protocol.h"
#include "ziliu/core/settings.h"
#include "ziliu/ipc/pipe_client.h"
#include "ziliu/ipc/pipe_server.h"

#include <windows.h>

#include <chrono>
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

class DelayedEngine final : public ziliu::core::Engine {
 public:
  DelayedEngine() : engine_(ziliu::core::CreateStubEngine()) {}

  void Reset() override { engine_->Reset(); }

  bool ProcessLetter(wchar_t letter) override {
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
    return engine_->ProcessLetter(letter);
  }

  bool ProcessSeparator() override { return engine_->ProcessSeparator(); }
  bool Backspace() override { return engine_->Backspace(); }
  bool PageUp() override { return engine_->PageUp(); }
  bool PageDown() override { return engine_->PageDown(); }

  void SetCandidatePageSize(std::size_t page_size) override {
    engine_->SetCandidatePageSize(page_size);
  }

  void SetCandidateWindowPageCount(std::size_t page_count) override {
    engine_->SetCandidateWindowPageCount(page_count);
  }

  void SetTraditional(bool enabled) override { engine_->SetTraditional(enabled); }

  void SetChineseCandidatesOnly(bool enabled) override {
    engine_->SetChineseCandidatesOnly(enabled);
  }

  ziliu::core::SelectionResult Select(std::size_t candidate_index) override {
    return engine_->Select(candidate_index);
  }

  [[nodiscard]] ziliu::core::CompositionSnapshot Snapshot() const override {
    return engine_->Snapshot();
  }

 private:
  std::unique_ptr<ziliu::core::Engine> engine_;
};

std::unique_ptr<ziliu::core::Engine> CreateDelayedEngine(bool restricted) {
  static_cast<void>(restricted);
  return std::make_unique<DelayedEngine>();
}

void Expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

}  // namespace

int main() {
  using ziliu::core::ipc::Command;
  using ziliu::core::ipc::Request;
  using ziliu::core::ipc::Response;
  using ziliu::core::ipc::Status;

  Response expected_pager_response;
  expected_pager_response.request_id = 7;
  expected_pager_response.session_id = 11;
  expected_pager_response.consumed = true;
  expected_pager_response.snapshot.preedit = L"shi";
  expected_pager_response.snapshot.candidates = {{L"是", L"shi", 1.0}};
  expected_pager_response.snapshot.has_previous_page = true;
  expected_pager_response.snapshot.has_next_page = true;
  std::vector<std::byte> encoded_pager_response;
  Expect(ziliu::core::ipc::EncodeResponse(expected_pager_response, &encoded_pager_response),
         "pager availability should encode into an IPC response");
  Response decoded_pager_response;
  Expect(ziliu::core::ipc::DecodeResponse(encoded_pager_response, &decoded_pager_response),
         "pager availability should decode from an IPC response");
  Expect(decoded_pager_response.snapshot.has_previous_page &&
             decoded_pager_response.snapshot.has_next_page,
         "both pager availability flags should survive the IPC round trip");

  Request legacy_request{41, 0, Command::kPing, 0};
  std::vector<std::byte> encoded_legacy_request;
  Expect(ziliu::core::ipc::EncodeRequest(
             legacy_request,
             ziliu::core::ipc::kOldestCompatibleProtocolVersion,
             &encoded_legacy_request),
         "the current broker should encode the previous compatible request");
  Request decoded_legacy_request;
  std::uint16_t decoded_legacy_request_version = 0;
  Expect(ziliu::core::ipc::DecodeRequest(
             encoded_legacy_request, &decoded_legacy_request,
             &decoded_legacy_request_version) &&
             decoded_legacy_request_version ==
                 ziliu::core::ipc::kOldestCompatibleProtocolVersion &&
             decoded_legacy_request.request_id == legacy_request.request_id,
         "the current broker should accept the previous compatible request");

  Response legacy_response = expected_pager_response;
  legacy_response.request_id = legacy_request.request_id;
  std::vector<std::byte> encoded_legacy_response;
  Expect(ziliu::core::ipc::EncodeResponse(
             legacy_response,
             ziliu::core::ipc::kOldestCompatibleProtocolVersion,
             &encoded_legacy_response),
         "the current broker should encode a response in the caller's version");
  Response decoded_legacy_response;
  std::uint16_t decoded_legacy_response_version = 0;
  Expect(ziliu::core::ipc::DecodeResponse(
             encoded_legacy_response, &decoded_legacy_response,
             &decoded_legacy_response_version) &&
             decoded_legacy_response_version ==
                 ziliu::core::ipc::kOldestCompatibleProtocolVersion &&
             decoded_legacy_response.request_id == legacy_request.request_id &&
             !decoded_legacy_response.snapshot.has_previous_page &&
             !decoded_legacy_response.snapshot.has_next_page,
         "v4 responses should omit pager flags while retaining the shared payload");

  const std::wstring pipe_name =
      L"\\\\.\\pipe\\Ziliu.Tests." + std::to_wstring(GetCurrentProcessId());
  ziliu::core::Settings preferences;
  preferences.input_mode_switch_key = ziliu::core::InputModeSwitchKey::kControl;
  preferences.candidate_layout = ziliu::core::CandidateLayout::kHorizontal;
  preferences.candidate_count = 7;
  preferences.candidate_chinese_font_family = "霞鹜文楷";
  preferences.custom_theme_scale_with_windows = false;
  preferences.active_theme_id = "sogou.test";
  const std::string configured = ziliu::core::SerializeSettings(preferences);
  std::atomic<int> settings_state = 0;
  std::atomic<int> settings_reads = 0;
  std::atomic<int> theme_reads = 0;
  std::atomic<int> menu_reads = 0;
  std::atomic<int> menu_actions = 0;
  ziliu::ipc::PipeServer server(pipe_name, CreateDelayedEngine,
      [&]() -> std::optional<std::string> {
        ++settings_reads;
        if (settings_state.load() == 2) {
          return std::nullopt;
        }
        return settings_state.load() == 0 ? configured : ziliu::core::SerializeSettings({});
      },
      [&](std::string_view theme_id, std::string_view resource,
          std::uint32_t offset) -> std::optional<std::vector<std::byte>> {
        ++theme_reads;
        if (theme_id != "sogou.test") {
          return std::nullopt;
        }
        if (resource.empty() && offset == 0) {
          return std::vector<std::byte>{std::byte{'{'}, std::byte{'}'}};
        }
        if (resource == "assets/a.png" && offset == 0) {
          return std::vector<std::byte>{std::byte{0}, std::byte{0xFF}};
        }
        if (resource == "assets/a.png" && offset == 2) {
          return std::vector<std::byte>{};
        }
        return std::nullopt;
      },
      [&](std::int32_t x, std::int32_t y) {
        ++menu_reads;
        return x == -100 && y == 700;
      },
      [&](std::uint32_t action) {
        ++menu_actions;
        return action == 1 || action == 2;
      });
  std::jthread server_thread([&server] { Expect(server.Run() == 0, "server should stop cleanly"); });
  ziliu::ipc::PipeClient client(pipe_name);

  std::uint64_t request_id = 1;
  std::optional<ziliu::core::ipc::Response> response;
  for (int attempt = 0; attempt < 100 && !response.has_value(); ++attempt) {
    response = client.Exchange(Request{request_id, 0, Command::kCreateSession, 0});
    if (!response.has_value()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
  }
  ++request_id;
  Expect(response.has_value(), "create session should receive a response");
  Expect(response->status == Status::kOk && response->session_id != 0,
         "create session should return an id");
  const std::uint64_t session_id = response->session_id;

  response = client.Exchange(Request{request_id++, 0, Command::kGetSettings, 0});
  Expect(response.has_value() && response->status == Status::kOk &&
             response->session_id == 0 && response->commit.empty() &&
             ziliu::core::ParseSettings(response->settings_text) == preferences,
         "authenticated settings should preserve candidate appearance preferences");
  settings_state = 1;
  response = client.Exchange(Request{request_id++, 0, Command::kGetSettings, 0});
  Expect(response.has_value() && response->status == Status::kOk &&
             ziliu::core::ParseSettings(response->settings_text) == ziliu::core::Settings{},
         "settings changes should be visible without recreating a session");
  settings_state = 2;
  response = client.Exchange(Request{request_id++, 0, Command::kGetSettings, 0});
  Expect(response.has_value() && response->status == Status::kInternalError,
         "settings read failure must not be reported as successful default settings");
  settings_state = 0;

  Request theme_request{request_id++, 0, Command::kGetThemeResource, 0};
  theme_request.theme_id = "sogou.test";
  theme_request.resource = "assets/a.png";
  std::vector<std::byte> theme_wire;
  Expect(!ziliu::core::ipc::EncodeRequest(theme_request, 7, &theme_wire),
         "theme transfer must require protocol v8");
  Expect(ziliu::core::ipc::EncodeRequest(theme_request, &theme_wire),
         "theme transfer should encode in protocol v8");
  Request decoded_theme_request;
  Expect(ziliu::core::ipc::DecodeRequest(theme_wire, &decoded_theme_request) &&
             decoded_theme_request.theme_id == theme_request.theme_id &&
             decoded_theme_request.resource == theme_request.resource,
         "theme identity and resource path should survive the wire round trip");
  response = client.Exchange(theme_request);
  Expect(response.has_value() && response->status == Status::kOk &&
             response->theme_chunk ==
                 std::vector<std::byte>{std::byte{0}, std::byte{0xFF}},
         "raw theme bytes must survive the authenticated pipe");
  theme_request.request_id = request_id++;
  theme_request.value = 2;
  response = client.Exchange(theme_request);
  Expect(response.has_value() && response->status == Status::kOk &&
             response->theme_chunk.empty(), "theme resource EOF should be explicit");
  theme_request.request_id = request_id++;
  theme_request.theme_id = "sogou.other";
  response = client.Exchange(theme_request);
  Expect(response.has_value() && response->status == Status::kInvalidRequest,
         "a resource outside the active theme should not be served");
  Expect(theme_reads.load() == 3, "only authenticated theme requests reach the provider");

  Request menu_request{request_id++, 0, Command::kOpenQuickMenu, 0};
  menu_request.point_x = -100;
  menu_request.point_y = 700;
  Expect(!ziliu::core::ipc::EncodeRequest(menu_request, 7, &theme_wire),
         "quick-menu launch must require protocol v8");
  Expect(ziliu::core::ipc::EncodeRequest(menu_request, &theme_wire),
         "quick-menu launch should encode in protocol v8");
  Request decoded_menu_request;
  Expect(ziliu::core::ipc::DecodeRequest(theme_wire, &decoded_menu_request) &&
             decoded_menu_request.point_x == -100 &&
             decoded_menu_request.point_y == 700,
         "signed monitor coordinates must survive the wire round trip");
  response = client.Exchange(menu_request);
  Expect(response.has_value() && response->status == Status::kOk &&
             menu_reads.load() == 1,
         "authenticated client should be able to open the user-session menu");
  menu_request.request_id = request_id++;
  menu_request.session_id = session_id;
  response = client.Exchange(menu_request);
  Expect(response.has_value() && response->status == Status::kUnsupported &&
             menu_reads.load() == 1,
         "menu launch may not borrow an engine session identifier");

  Request action_request{request_id++, 0, Command::kRunMenuAction, 1};
  Expect(!ziliu::core::ipc::EncodeRequest(action_request, 7, &theme_wire),
         "menu actions require protocol v8");
  response = client.Exchange(action_request);
  Expect(response.has_value() && response->status == Status::kOk &&
             menu_actions.load() == 1,
         "authenticated menu action should reach its provider");
  action_request.request_id = request_id++;
  action_request.value = 3;
  response = client.Exchange(action_request);
  Expect(response.has_value() && response->status == Status::kUnsupported &&
             menu_actions.load() == 1,
         "unknown menu action must not reach its provider");
  action_request.request_id = request_id++;
  action_request.session_id = session_id;
  action_request.value = 2;
  response = client.Exchange(action_request);
  Expect(response.has_value() && response->status == Status::kUnsupported &&
             menu_actions.load() == 1,
         "menu action may not borrow an engine session identifier");

  Response settings_response;
  settings_response.settings_text = configured;
  std::vector<std::byte> settings_bytes;
  Expect(ziliu::core::ipc::EncodeResponse(settings_response, 5, &settings_bytes),
         "old v5 clients should still receive their original wire format");
  Response old_settings_response;
  Expect(ziliu::core::ipc::DecodeResponse(settings_bytes, &old_settings_response) &&
             old_settings_response.settings_text.empty(), "v5 must omit v6 settings payload");
  Expect(ziliu::core::ipc::EncodeResponse(settings_response, &settings_bytes),
         "v6 settings should encode");
  settings_bytes.pop_back();
  Expect(!ziliu::core::ipc::DecodeResponse(settings_bytes, &old_settings_response),
         "truncated settings must be rejected");
  settings_response.settings_text.assign(16385, 'x');
  Expect(!ziliu::core::ipc::EncodeResponse(settings_response, &settings_bytes),
         "settings payload must have a fixed size limit");
  settings_response.settings_text = "\xff";
  Expect(ziliu::core::ipc::EncodeResponse(settings_response, &settings_bytes) &&
             !ziliu::core::ipc::DecodeResponse(settings_bytes, &old_settings_response),
         "invalid UTF-8 settings must be rejected");
  Expect(!ziliu::core::ipc::EncodeRequest(Request{1, 0, Command::kGetSettings, 0}, 5,
                                         &settings_bytes), "settings query requires v6");
  const Request restricted_create{2, 0, Command::kCreateSession, 1};
  Expect(!ziliu::core::ipc::EncodeRequest(restricted_create, 6, &settings_bytes),
         "restricted session requires protocol v7");
  Expect(ziliu::core::ipc::EncodeRequest(restricted_create, &settings_bytes),
         "restricted session should encode in protocol v7");
  settings_bytes[4] = std::byte{6};
  settings_bytes[5] = std::byte{0};
  Request downgraded_restricted;
  Expect(!ziliu::core::ipc::DecodeRequest(settings_bytes, &downgraded_restricted),
         "the current parser must reject a forged v6 restricted-create request");

  for (const wchar_t letter : std::wstring_view(L"ziliu")) {
    response = client.Exchange(
        Request{request_id++, session_id, Command::kInputLetter,
                static_cast<std::uint32_t>(letter)});
    Expect(response.has_value(), "a 25 ms letter response should not time out");
    Expect(response->status == Status::kOk, "letter request should keep the session");
    Expect(response->session_id == session_id, "letter response should match the session");
    Expect(response->consumed, "letters should be consumed");
  }
  Expect(response->snapshot.preedit == L"ziliu", "preedit should cross the pipe");
  Expect(!response->snapshot.candidates.empty() &&
             response->snapshot.candidates.front().text == L"字流",
         "UTF-8 candidates should cross the pipe");

  response = client.Exchange(Request{request_id++, session_id, Command::kSelectCandidate, 0});
  Expect(response.has_value() && response->commit == L"字流", "selection should commit 字流");
  Expect(response->snapshot.empty(), "commit should clear the composition");

  response = client.Exchange(Request{request_id++, session_id, Command::kCloseSession, 0});
  Expect(response.has_value() && response->status == Status::kOk,
         "close session should succeed");

  // An anonymous SQOS client must not gain access through the AppContainer ACE.
  Expect(WaitNamedPipeW(pipe_name.c_str(), 1000) != FALSE, "pipe should be available");
  HANDLE anonymous = CreateFileW(pipe_name.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
      nullptr, OPEN_EXISTING, SECURITY_SQOS_PRESENT | SECURITY_ANONYMOUS, nullptr);
  Expect(anonymous != INVALID_HANDLE_VALUE, "anonymous test should reach authentication");
  DWORD mode = PIPE_READMODE_MESSAGE;
  Expect(SetNamedPipeHandleState(anonymous, &mode, nullptr, nullptr) != FALSE,
         "anonymous test should use message mode");
  std::vector<std::byte> ping;
  const int reads_before_anonymous = settings_reads.load();
  Expect(ziliu::core::ipc::EncodeRequest(Request{request_id++, 0, Command::kGetSettings, 0}, &ping),
         "anonymous ping should encode");
  std::byte reply[256]{};
  DWORD reply_size = 0;
  const BOOL anonymous_result = TransactNamedPipe(anonymous, ping.data(),
      static_cast<DWORD>(ping.size()), reply, sizeof(reply), &reply_size, nullptr);
  CloseHandle(anonymous);
  Expect(!anonymous_result, "anonymous client must be disconnected without an engine response");
  Expect(settings_reads.load() == reads_before_anonymous,
         "anonymous client must not invoke the settings provider");
  response = client.Exchange(Request{request_id++, 0, Command::kPing, 0});
  Expect(response.has_value() && response->status == Status::kOk,
         "authorized client should still work after rejecting anonymous client");

  server.Stop();
  server_thread.join();
  std::cout << "ziliu_ipc_tests: OK\n";
  return EXIT_SUCCESS;
}
