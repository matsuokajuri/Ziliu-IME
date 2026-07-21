#include "pch.h"

#include "App.xaml.h"
#include "MainWindow.xaml.h"

#include <shellapi.h>

#include <string_view>

namespace winrt::ZiliuSettings::implementation {
namespace {

constexpr wchar_t kQuickMenuMutexName[] = L"Local\\Ziliu.Settings.QuickMenu";

bool IsQuickMenuLaunch() {
  int argument_count = 0;
  wchar_t** arguments = CommandLineToArgvW(GetCommandLineW(), &argument_count);
  if (arguments == nullptr) {
    return false;
  }

  bool quick_menu = false;
  for (int index = 1; index < argument_count; ++index) {
    if (std::wstring_view(arguments[index]) == L"--quick-menu") {
      quick_menu = true;
      break;
    }
  }
  LocalFree(arguments);
  return quick_menu;
}

}  // namespace

App::App() { InitializeComponent(); }

void App::OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const&) {
  const bool quick_menu = IsQuickMenuLaunch();
  if (quick_menu) {
    quick_menu_mutex_ = CreateMutexW(nullptr, TRUE, kQuickMenuMutexName);
    if (quick_menu_mutex_ != nullptr && GetLastError() == ERROR_ALREADY_EXISTS) {
      CloseHandle(quick_menu_mutex_);
      quick_menu_mutex_ = nullptr;
      Exit();
      return;
    }
  }

  window_ = winrt::make<MainWindow>();
  if (quick_menu) {
    window_.Closed(
        [this](winrt::Windows::Foundation::IInspectable const&,
               Microsoft::UI::Xaml::WindowEventArgs const&) {
          ReleaseQuickMenuInstance();
          window_ = nullptr;
          Exit();
        });
  }
  window_.Activate();
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
