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
  void PrepareQuickMenuOpenAnimation();
  void PlayQuickMenuOpenAnimation();
  void InitializeNavigation();
  void ShowSettingsPage(std::wstring_view page);
  void ApplyThemeFromControls();
  void UpdateAppearanceControlStates();
  void UpdateColorSwatches();
  void SaveFromControls();

  ziliu::core::Settings settings_;
  bool quick_menu_animation_started_ = false;
};

}  // namespace winrt::ZiliuSettings::implementation

namespace winrt::ZiliuSettings::factory_implementation {

struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow> {};

}  // namespace winrt::ZiliuSettings::factory_implementation
