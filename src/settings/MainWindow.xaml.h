#pragma once

#include "MainWindow.g.h"
#include "pch.h"

#include "ziliu/core/settings.h"
#include "ziliu/core/theme_catalog.h"
#include "ziliu/ui/candidate_window.h"

#include <optional>
#include <string_view>
#include <vector>

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
  void UpdateAppearanceResponsiveLayout();
  void UpdateColorSwatches();
  void UpdateCandidatePreview();
  void EnsureCandidatePreview();
  [[nodiscard]] std::optional<RECT> CandidatePreviewBounds(RECT& viewport_bounds);
  void ReloadThemeCatalog();
  void RebuildThemeList();
  bool SelectTheme(std::string_view theme_id);
  winrt::fire_and_forget ImportTheme();
  winrt::fire_and_forget DeleteTheme(std::string theme_id);
  bool SaveFromControls();

  ziliu::core::Settings settings_;
  ziliu::ui::CandidateWindow candidate_preview_;
  std::vector<ziliu::core::InstalledTheme> installed_themes_;
  bool candidate_preview_ready_ = false;
  bool candidate_preview_updating_ = false;
  bool candidate_preview_closed_ = false;
  bool suppress_appearance_events_ = false;
  std::optional<bool> appearance_layout_narrow_;
  bool theme_dialog_open_ = false;
  bool quick_menu_animation_started_ = false;
  Microsoft::UI::Dispatching::DispatcherQueueTimer quick_menu_close_arm_timer_{nullptr};
};

}  // namespace winrt::ZiliuSettings::implementation

namespace winrt::ZiliuSettings::factory_implementation {

struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow> {};

}  // namespace winrt::ZiliuSettings::factory_implementation
