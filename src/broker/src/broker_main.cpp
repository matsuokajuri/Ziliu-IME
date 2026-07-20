#include <windows.h>

#include <string>

namespace {

constexpr wchar_t kBrokerWindowClass[] = L"Ziliu.Broker.MessageWindow.v1";
constexpr wchar_t kBrokerMutexName[] = L"Local\\Ziliu.Broker.Singleton.v1";

LRESULT CALLBACK BrokerWindowProcedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  if (message == WM_CLOSE) {
    DestroyWindow(window);
    return 0;
  }
  if (message == WM_DESTROY) {
    PostQuitMessage(0);
    return 0;
  }
  return DefWindowProcW(window, message, wparam, lparam);
}

int RunBroker(HINSTANCE instance) {
  HANDLE mutex = CreateMutexW(nullptr, TRUE, kBrokerMutexName);
  if (mutex == nullptr) {
    return 1;
  }
  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    CloseHandle(mutex);
    return 0;
  }

  WNDCLASSEXW window_class{sizeof(window_class)};
  window_class.lpfnWndProc = BrokerWindowProcedure;
  window_class.hInstance = instance;
  window_class.lpszClassName = kBrokerWindowClass;
  if (RegisterClassExW(&window_class) == 0) {
    CloseHandle(mutex);
    return 2;
  }

  const HWND message_window =
      CreateWindowExW(0, kBrokerWindowClass, L"Ziliu Broker", 0, 0, 0, 0, 0, HWND_MESSAGE,
                      nullptr, instance, nullptr);
  if (message_window == nullptr) {
    CloseHandle(mutex);
    return 3;
  }

  MSG message{};
  while (GetMessageW(&message, nullptr, 0, 0) > 0) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }

  CloseHandle(mutex);
  return static_cast<int>(message.wParam);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous_instance, wchar_t* command_line,
                    int show_command) {
  static_cast<void>(previous_instance);
  static_cast<void>(command_line);
  static_cast<void>(show_command);
  return RunBroker(instance);
}

