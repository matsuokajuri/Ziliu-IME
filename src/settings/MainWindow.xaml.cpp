#include "pch.h"

#include "MainWindow.xaml.h"

#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"

#include "ziliu/core/settings.h"

#include <microsoft.ui.xaml.window.h>
#include <shellapi.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>

namespace winrt::ZiliuSettings::implementation {
namespace {

struct LaunchOptions {
  bool quick_menu = false;
  int anchor_x = 0;
  int anchor_y = 0;
};

std::filesystem::path ExecutablePath() {
  std::wstring executable(32768, L'\0');
  const DWORD length =
      GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
  if (length == 0 || static_cast<std::size_t>(length) >= executable.size()) {
    return {};
  }
  executable.resize(length);
  return executable;
}

std::optional<std::filesystem::path> SettingsFilePath() {
  std::wstring local_app_data(32768, L'\0');
  const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", local_app_data.data(),
                                               static_cast<DWORD>(local_app_data.size()));
  if (length == 0 || static_cast<std::size_t>(length) >= local_app_data.size()) {
    return std::nullopt;
  }
  local_app_data.resize(length);
  return std::filesystem::path(local_app_data) / L"Ziliu" / L"settings.ini";
}

ziliu::core::Settings LoadSettings() {
  const auto path = SettingsFilePath();
  if (!path.has_value()) {
    return {};
  }
  std::ifstream stream(*path, std::ios::binary);
  if (!stream) {
    return {};
  }
  const std::string contents{std::istreambuf_iterator<char>(stream),
                             std::istreambuf_iterator<char>()};
  return ziliu::core::ParseSettings(contents);
}

bool SaveSettings(const ziliu::core::Settings& settings) {
  const auto path = SettingsFilePath();
  if (!path.has_value()) {
    return false;
  }
  std::error_code directory_error;
  std::filesystem::create_directories(path->parent_path(), directory_error);
  if (directory_error) {
    return false;
  }
  std::ofstream stream(*path, std::ios::binary | std::ios::trunc);
  if (!stream) {
    return false;
  }
  const std::string serialized = ziliu::core::SerializeSettings(settings);
  stream.write(serialized.data(), static_cast<std::streamsize>(serialized.size()));
  stream.flush();
  return stream.good();
}

LaunchOptions ParseLaunchOptions() {
  LaunchOptions options;
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

}  // namespace

MainWindow::MainWindow() {
  InitializeComponent();
  settings_ = LoadSettings();

  const LaunchOptions options = ParseLaunchOptions();
  if (options.quick_menu) {
    Title(L"字流 Ziliu");
    SettingsRoot().Visibility(Microsoft::UI::Xaml::Visibility::Collapsed);
    QuickMenuRoot().Visibility(Microsoft::UI::Xaml::Visibility::Visible);
    InitializeQuickMenuControls();
  } else {
    Title(L"字流 Ziliu 设置");
    SettingsRoot().Visibility(Microsoft::UI::Xaml::Visibility::Visible);
    QuickMenuRoot().Visibility(Microsoft::UI::Xaml::Visibility::Collapsed);
    InitializeSettingsControls();
    AppWindow().Closing(
        [this](Microsoft::UI::Windowing::AppWindow const&,
               Microsoft::UI::Windowing::AppWindowClosingEventArgs const&) {
          SaveFromControls();
        });
  }
  ConfigureWindow(options.quick_menu, options.anchor_x, options.anchor_y);
  if (options.quick_menu) {
    Activated(
        [this](winrt::Windows::Foundation::IInspectable const&,
               Microsoft::UI::Xaml::WindowActivatedEventArgs const& args) {
          if (args.WindowActivationState() ==
              Microsoft::UI::Xaml::WindowActivationState::Deactivated) {
            Close();
          }
        });
  }
}

void MainWindow::ConfigureWindow(bool quick_menu, int anchor_x, int anchor_y) {
  HWND window_handle = nullptr;
  Microsoft::UI::Xaml::Window window = *this;
  winrt::check_hresult(window.as<::IWindowNative>()->get_WindowHandle(&window_handle));
  const UINT dpi =
      std::max(GetDpiForWindow(window_handle), static_cast<UINT>(USER_DEFAULT_SCREEN_DPI));
  const auto scaled = [dpi](int value) {
    return MulDiv(value, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI);
  };

  if (!quick_menu) {
    ExtendsContentIntoTitleBar(true);
    SetTitleBar(SettingsTitleBar());
    SetWindowPos(window_handle, nullptr, 0, 0, scaled(860), scaled(780),
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    return;
  }

  LONG_PTR style = GetWindowLongPtrW(window_handle, GWL_STYLE);
  style &= ~(static_cast<LONG_PTR>(WS_CAPTION) | static_cast<LONG_PTR>(WS_THICKFRAME) |
             static_cast<LONG_PTR>(WS_MINIMIZEBOX) | static_cast<LONG_PTR>(WS_MAXIMIZEBOX) |
             static_cast<LONG_PTR>(WS_SYSMENU));
  style |= WS_POPUP;
  SetWindowLongPtrW(window_handle, GWL_STYLE, style);

  LONG_PTR extended_style = GetWindowLongPtrW(window_handle, GWL_EXSTYLE);
  extended_style &= ~static_cast<LONG_PTR>(WS_EX_APPWINDOW);
  extended_style |= WS_EX_TOOLWINDOW;
  SetWindowLongPtrW(window_handle, GWL_EXSTYLE, extended_style);

  const int width = scaled(360);
  const int height = scaled(276);
  const POINT anchor{anchor_x, anchor_y};
  const HMONITOR monitor = MonitorFromPoint(anchor, MONITOR_DEFAULTTONEAREST);
  MONITORINFO monitor_info{sizeof(monitor_info)};
  if (!GetMonitorInfoW(monitor, &monitor_info)) {
    monitor_info.rcWork = RECT{0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
  }
  const int work_left = static_cast<int>(monitor_info.rcWork.left);
  const int work_top = static_cast<int>(monitor_info.rcWork.top);
  const int work_right = static_cast<int>(monitor_info.rcWork.right);
  const int work_bottom = static_cast<int>(monitor_info.rcWork.bottom);
  const int x = std::clamp(anchor_x - width / 2, work_left, work_right - width);
  const int preferred_y = anchor_y - height - scaled(8);
  const int y = preferred_y >= work_top
                    ? preferred_y
                    : std::min(anchor_y + scaled(36), work_bottom - height);
  SetWindowPos(window_handle, HWND_TOP, x, y, width, height,
               SWP_FRAMECHANGED | SWP_NOACTIVATE);
}

void MainWindow::InitializeSettingsControls() {
  LayoutCombo().SelectedIndex(settings_.candidate_layout ==
                                      ziliu::core::CandidateLayout::kHorizontal
                                  ? 1
                                  : 0);
  CandidateCountCombo().SelectedIndex(static_cast<int>(settings_.candidate_count) - 3);
  SwitchKeyCombo().SelectedIndex(
      settings_.input_mode_switch_key == ziliu::core::InputModeSwitchKey::kControl ? 1 : 0);
  PunctuationToggle().IsOn(settings_.punctuation_style ==
                           ziliu::core::PunctuationStyle::kFullWidth);
  AutoPairToggle().IsOn(settings_.auto_pair_punctuation);
  int page_key_index = 0;
  if (settings_.page_key_set == ziliu::core::PageKeySet::kSemicolonApostrophe) {
    page_key_index = 1;
  } else if (settings_.page_key_set == ziliu::core::PageKeySet::kBrackets) {
    page_key_index = 2;
  }
  PageKeyCombo().SelectedIndex(page_key_index);
}

void MainWindow::InitializeQuickMenuControls() {
  CharacterSetToggle().IsOn(settings_.character_set ==
                            ziliu::core::CharacterSet::kTraditional);
  CharacterSetToggle().Toggled(
      [this](winrt::Windows::Foundation::IInspectable const&,
             Microsoft::UI::Xaml::RoutedEventArgs const&) {
        settings_.character_set = CharacterSetToggle().IsOn()
                                      ? ziliu::core::CharacterSet::kTraditional
                                      : ziliu::core::CharacterSet::kSimplified;
        static_cast<void>(SaveSettings(settings_));
      });
  OpenSettingsButton().Click(
      [this](winrt::Windows::Foundation::IInspectable const&,
             Microsoft::UI::Xaml::RoutedEventArgs const&) {
        const std::filesystem::path executable = ExecutablePath();
        ShellExecuteW(nullptr, L"open", executable.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        Close();
      });
}

void MainWindow::SaveFromControls() {
  settings_.candidate_layout = LayoutCombo().SelectedIndex() == 1
                                   ? ziliu::core::CandidateLayout::kHorizontal
                                   : ziliu::core::CandidateLayout::kVertical;
  settings_.candidate_count = static_cast<std::size_t>(CandidateCountCombo().SelectedIndex() + 3);
  settings_.input_mode_switch_key =
      SwitchKeyCombo().SelectedIndex() == 1 ? ziliu::core::InputModeSwitchKey::kControl
                                            : ziliu::core::InputModeSwitchKey::kShift;
  settings_.punctuation_style = PunctuationToggle().IsOn()
                                    ? ziliu::core::PunctuationStyle::kFullWidth
                                    : ziliu::core::PunctuationStyle::kHalfWidth;
  settings_.auto_pair_punctuation = AutoPairToggle().IsOn();
  if (PageKeyCombo().SelectedIndex() == 1) {
    settings_.page_key_set = ziliu::core::PageKeySet::kSemicolonApostrophe;
  } else if (PageKeyCombo().SelectedIndex() == 2) {
    settings_.page_key_set = ziliu::core::PageKeySet::kBrackets;
  } else {
    settings_.page_key_set = ziliu::core::PageKeySet::kCommaPeriod;
  }

  static_cast<void>(SaveSettings(settings_));
}

}  // namespace winrt::ZiliuSettings::implementation

#endif
