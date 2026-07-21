#pragma once

#include "App.xaml.g.h"
#include "pch.h"

namespace winrt::ZiliuSettings::implementation {

struct App : AppT<App> {
  App();

  void OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const&);

 private:
  void ReleaseQuickMenuInstance();

  Microsoft::UI::Xaml::Window window_{nullptr};
  HANDLE quick_menu_mutex_ = nullptr;
};

}  // namespace winrt::ZiliuSettings::implementation
