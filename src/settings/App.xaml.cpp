#include "pch.h"

#include "App.xaml.h"
#include "MainWindow.xaml.h"

#include <microsoft.ui.xaml.window.h>
#include <shellapi.h>

#include <algorithm>
#include <string_view>

namespace winrt::ZiliuSettings::implementation {
namespace {

constexpr wchar_t kQuickMenuMutexName[] = L"Local\\Ziliu.Settings.QuickMenu";
constexpr wchar_t kQuickMenuWindowTitle[] = L"字流 Ziliu";

struct QuickMenuLaunchOptions {
  bool quick_menu = false;
  int anchor_x = 0;
  int anchor_y = 0;
};

QuickMenuLaunchOptions ParseQuickMenuLaunchOptions() {
  QuickMenuLaunchOptions options;
  int argument_count = 0;
  wchar_t** arguments = CommandLineToArgvW(GetCommandLineW(), &argument_count);
  if (arguments == nullptr) {
    return options;
  }

  for (int index = 1; index < argument_count; ++index) {
    const std::wstring_view argument(arguments[index]);
    if (argument == L"--quick-menu") {
      options.quick_menu = true;
    } else if (argument == L"--x" && index + 1 < argument_count) {
      options.anchor_x = _wtoi(arguments[++index]);
    } else if (argument == L"--y" && index + 1 < argument_count) {
      options.anchor_y = _wtoi(arguments[++index]);
    }
  }
  LocalFree(arguments);
  return options;
}

bool ActivateExistingQuickMenu(const QuickMenuLaunchOptions& options) {
  const HWND window = FindWindowW(nullptr, kQuickMenuWindowTitle);
  if (window == nullptr) {
    return false;
  }

  RECT window_rectangle{};
  if (!GetWindowRect(window, &window_rectangle)) {
    return false;
  }
  const int width =
      std::max(static_cast<int>(window_rectangle.right - window_rectangle.left), 1);
  const int height =
      std::max(static_cast<int>(window_rectangle.bottom - window_rectangle.top), 1);
  const POINT anchor{options.anchor_x, options.anchor_y};
  const HMONITOR monitor = MonitorFromPoint(anchor, MONITOR_DEFAULTTONEAREST);
  MONITORINFO monitor_info{sizeof(monitor_info)};
  if (!GetMonitorInfoW(monitor, &monitor_info)) {
    monitor_info.rcWork = RECT{0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
  }

  const int work_left = static_cast<int>(monitor_info.rcWork.left);
  const int work_top = static_cast<int>(monitor_info.rcWork.top);
  const int work_right = static_cast<int>(monitor_info.rcWork.right);
  const int work_bottom = static_cast<int>(monitor_info.rcWork.bottom);
  const int x = std::clamp(options.anchor_x - width / 2, work_left, work_right - width);
  const int preferred_y = options.anchor_y - height - 8;
  const int y = preferred_y >= work_top ? preferred_y
                                        : std::min(options.anchor_y + 36, work_bottom - height);
  SetWindowPos(window, HWND_TOPMOST, x, std::clamp(y, work_top, work_bottom - height), width,
               height, SWP_SHOWWINDOW);
  ShowWindow(window, SW_SHOWNORMAL);
  static_cast<void>(SetForegroundWindow(window));
  return true;
}

}  // namespace

App::App() { InitializeComponent(); }

void App::OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const&) {
  const QuickMenuLaunchOptions options = ParseQuickMenuLaunchOptions();
  if (options.quick_menu) {
    quick_menu_mutex_ = CreateMutexW(nullptr, TRUE, kQuickMenuMutexName);
    if (quick_menu_mutex_ != nullptr && GetLastError() == ERROR_ALREADY_EXISTS) {
      CloseHandle(quick_menu_mutex_);
      quick_menu_mutex_ = nullptr;
      static_cast<void>(ActivateExistingQuickMenu(options));
      Exit();
      return;
    }
  }

  window_ = winrt::make<MainWindow>();
  if (options.quick_menu) {
    window_.Closed(
        [this](winrt::Windows::Foundation::IInspectable const&,
               Microsoft::UI::Xaml::WindowEventArgs const&) {
          ReleaseQuickMenuInstance();
          window_ = nullptr;
          Exit();
        });
  }
  window_.Activate();
  if (options.quick_menu) {
    HWND window_handle = nullptr;
    if (SUCCEEDED(window_.as<::IWindowNative>()->get_WindowHandle(&window_handle)) &&
        window_handle != nullptr) {
      ShowWindow(window_handle, SW_SHOWNORMAL);
      static_cast<void>(SetForegroundWindow(window_handle));
    }
  }
}

void App::ReleaseQuickMenuInstance() {
  if (quick_menu_mutex_ == nullptr) {
    return;
  }
  ReleaseMutex(quick_menu_mutex_);
  CloseHandle(quick_menu_mutex_);
  quick_menu_mutex_ = nullptr;
}

}  // namespace winrt::ZiliuSettings::implementation
