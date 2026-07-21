#pragma once

#include "MainWindow.g.h"
#include "pch.h"

#include "ziliu/core/settings.h"

namespace winrt::ZiliuSettings::implementation {

struct MainWindow : MainWindowT<MainWindow> {
  MainWindow();

 private:
  void ConfigureWindow(bool quick_menu, int anchor_x, int anchor_y);
  void InitializeSettingsControls();
  void InitializeQuickMenuControls();
  void SaveFromControls();

  ziliu::core::Settings settings_;
};

}  // namespace winrt::ZiliuSettings::implementation

namespace winrt::ZiliuSettings::factory_implementation {

struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow> {};

}  // namespace winrt::ZiliuSettings::factory_implementation
