#include "pch.h"

#include "MainWindow.xaml.h"

#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"

#include "ziliu/core/settings.h"

#include <dwmapi.h>
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

winrt::Windows::UI::Color ToColor(std::uint32_t rgb) {
  winrt::Windows::UI::Color color{};
  color.A = 0xFF;
  color.R = static_cast<std::uint8_t>((rgb >> 16) & 0xFF);
  color.G = static_cast<std::uint8_t>((rgb >> 8) & 0xFF);
  color.B = static_cast<std::uint8_t>(rgb & 0xFF);
  return color;
}

std::uint32_t FromColor(winrt::Windows::UI::Color color) {
  return (static_cast<std::uint32_t>(color.R) << 16) |
         (static_cast<std::uint32_t>(color.G) << 8) | static_cast<std::uint32_t>(color.B);
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
    InitializeNavigation();
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
    SetWindowPos(window_handle, nullptr, 0, 0, scaled(1100), scaled(820),
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

  const int width = scaled(344);
  const int height = scaled(260);
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

  const DWMNCRENDERINGPOLICY rendering_policy = DWMNCRP_ENABLED;
  static_cast<void>(DwmSetWindowAttribute(window_handle, DWMWA_NCRENDERING_POLICY,
                                          &rendering_policy, sizeof(rendering_policy)));
  const DWM_WINDOW_CORNER_PREFERENCE corner_preference = DWMWCP_ROUND;
  const HRESULT corner_result =
      DwmSetWindowAttribute(window_handle, DWMWA_WINDOW_CORNER_PREFERENCE, &corner_preference,
                            sizeof(corner_preference));
  const COLORREF border_color = DWMWA_COLOR_NONE;
  static_cast<void>(DwmSetWindowAttribute(window_handle, DWMWA_BORDER_COLOR, &border_color,
                                          sizeof(border_color)));
  const MARGINS shadow_margins{1, 1, 1, 1};
  static_cast<void>(DwmExtendFrameIntoClientArea(window_handle, &shadow_margins));

  if (FAILED(corner_result)) {
    const int corner_diameter = scaled(20);
    HRGN window_region =
        CreateRoundRectRgn(0, 0, width + 1, height + 1, corner_diameter, corner_diameter);
    if (window_region != nullptr && SetWindowRgn(window_handle, window_region, FALSE) == 0) {
      DeleteObject(window_region);
    }
  }
}

void MainWindow::InitializeSettingsControls() {
  CharacterSetCombo().SelectedIndex(settings_.character_set ==
                                            ziliu::core::CharacterSet::kTraditional
                                        ? 1
                                        : 0);
  PunctuationCombo().SelectedIndex(settings_.punctuation_style ==
                                           ziliu::core::PunctuationStyle::kFullWidth
                                       ? 1
                                       : 0);
  DefaultInputModeCombo().SelectedIndex(settings_.default_input_mode ==
                                                ziliu::core::DefaultInputMode::kEnglish
                                            ? 1
                                            : 0);
  InitialismToggle().IsOn(settings_.initialism_spelling);
  SpellingCorrectionToggle().IsOn(settings_.spelling_correction);
  AutoPairToggle().IsOn(settings_.auto_pair_punctuation);
  SmartNumericPunctuationToggle().IsOn(settings_.smart_numeric_punctuation);

  const auto set_checked = [](Microsoft::UI::Xaml::Controls::CheckBox const& control,
                              bool checked) {
    control.IsChecked(winrt::box_value(checked).as<winrt::Windows::Foundation::IReference<bool>>());
  };
  set_checked(CorrectionGnNgCheck(), settings_.correction_gn_ng);
  set_checked(CorrectionMgNgCheck(), settings_.correction_mg_ng);
  set_checked(CorrectionIouIuCheck(), settings_.correction_iou_iu);
  set_checked(CorrectionUeiUiCheck(), settings_.correction_uei_ui);
  set_checked(CorrectionUenUnCheck(), settings_.correction_uen_un);
  set_checked(FuzzyZZhCheck(), settings_.fuzzy_z_zh);
  set_checked(FuzzyCChCheck(), settings_.fuzzy_c_ch);
  set_checked(FuzzySShCheck(), settings_.fuzzy_s_sh);
  set_checked(FuzzyLNCheck(), settings_.fuzzy_l_n);
  set_checked(FuzzyFHCheck(), settings_.fuzzy_f_h);
  set_checked(FuzzyRLCheck(), settings_.fuzzy_r_l);
  set_checked(FuzzyAnAngCheck(), settings_.fuzzy_an_ang);
  set_checked(FuzzyEnEngCheck(), settings_.fuzzy_en_eng);
  set_checked(FuzzyInIngCheck(), settings_.fuzzy_in_ing);
  set_checked(FuzzyIanIangCheck(), settings_.fuzzy_ian_iang);
  set_checked(FuzzyUanUangCheck(), settings_.fuzzy_uan_uang);

  ThemeCombo().SelectedIndex(static_cast<int>(settings_.theme_mode));
  LayoutCombo().SelectedIndex(settings_.candidate_layout ==
                                      ziliu::core::CandidateLayout::kHorizontal
                                  ? 0
                                  : 1);
  CandidateCountCombo().SelectedIndex(static_cast<int>(settings_.candidate_count) - 3);
  CandidatePageModeCombo().SelectedIndex(settings_.candidate_page_mode ==
                                                 ziliu::core::CandidatePageMode::kMultiLine
                                             ? 1
                                             : 0);
  CustomColorsToggle().IsOn(settings_.custom_candidate_colors);
  PreeditColorPicker().Color(ToColor(settings_.preedit_color));
  HighlightedColorPicker().Color(ToColor(settings_.highlighted_candidate_color));
  CandidateTextColorPicker().Color(ToColor(settings_.candidate_text_color));
  BackgroundColorPicker().Color(ToColor(settings_.candidate_background_color));
  CustomFontsToggle().IsOn(settings_.custom_candidate_fonts);
  CandidateChineseFontCombo().SelectedIndex(
      static_cast<int>(settings_.candidate_chinese_font_family));
  CandidateEnglishFontCombo().SelectedIndex(
      static_cast<int>(settings_.candidate_english_font_family));
  CustomFontSizeToggle().IsOn(settings_.custom_candidate_font_size);
  CandidateFontSizeCombo().SelectedIndex(static_cast<int>(settings_.candidate_font_size) - 14);
  CandidateScaleToggle().IsOn(settings_.candidate_scale_with_text);
  SwitchKeyCombo().SelectedIndex(
      settings_.input_mode_switch_key == ziliu::core::InputModeSwitchKey::kControl ? 1 : 0);
  int page_key_index = 0;
  if (settings_.page_key_set == ziliu::core::PageKeySet::kSemicolonApostrophe) {
    page_key_index = 1;
  } else if (settings_.page_key_set == ziliu::core::PageKeySet::kBrackets) {
    page_key_index = 2;
  }
  PageKeyCombo().SelectedIndex(page_key_index);
  ApplyThemeFromControls();
  UpdateAppearanceControlStates();
  UpdateColorSwatches();
}

void MainWindow::InitializeNavigation() {
  SettingsNavigation().SelectionChanged(
      [this](winrt::Windows::Foundation::IInspectable const&,
             Microsoft::UI::Xaml::Controls::NavigationViewSelectionChangedEventArgs const& args) {
        const auto item = args.SelectedItemContainer();
        if (item != nullptr) {
          ShowSettingsPage(winrt::unbox_value_or<winrt::hstring>(item.Tag(), L"common"));
        }
      });
  CorrectionSettingsButton().Click(
      [this](auto const&, auto const&) { ShowSettingsPage(L"correction"); });
  FuzzySettingsButton().Click(
      [this](auto const&, auto const&) { ShowSettingsPage(L"fuzzy"); });
  PunctuationSettingsButton().Click(
      [this](auto const&, auto const&) { ShowSettingsPage(L"punctuation"); });
  CorrectionBackButton().Click([this](auto const&, auto const&) { ShowSettingsPage(L"common"); });
  FuzzyBackButton().Click([this](auto const&, auto const&) { ShowSettingsPage(L"common"); });
  PunctuationBackButton().Click(
      [this](auto const&, auto const&) { ShowSettingsPage(L"common"); });
  ThemeCombo().SelectionChanged([this](auto const&, auto const&) { ApplyThemeFromControls(); });
  CustomColorsToggle().Toggled(
      [this](auto const&, auto const&) { UpdateAppearanceControlStates(); });
  CustomFontsToggle().Toggled(
      [this](auto const&, auto const&) { UpdateAppearanceControlStates(); });
  CustomFontSizeToggle().Toggled(
      [this](auto const&, auto const&) { UpdateAppearanceControlStates(); });
  const auto update_color = [this](auto const&, auto const&) { UpdateColorSwatches(); };
  PreeditColorPicker().ColorChanged(update_color);
  HighlightedColorPicker().ColorChanged(update_color);
  CandidateTextColorPicker().ColorChanged(update_color);
  BackgroundColorPicker().ColorChanged(update_color);
  ResetAppearanceButton().Click([this](auto const&, auto const&) {
    ThemeCombo().SelectedIndex(0);
    LayoutCombo().SelectedIndex(1);
    CandidateCountCombo().SelectedIndex(2);
    CandidatePageModeCombo().SelectedIndex(0);
    CustomColorsToggle().IsOn(false);
    PreeditColorPicker().Color(ToColor(0x202124));
    HighlightedColorPicker().Color(ToColor(0x0067C0));
    CandidateTextColorPicker().Color(ToColor(0x202124));
    BackgroundColorPicker().Color(ToColor(0xFAFAFA));
    CustomFontsToggle().IsOn(false);
    CandidateChineseFontCombo().SelectedIndex(0);
    CandidateEnglishFontCombo().SelectedIndex(0);
    CustomFontSizeToggle().IsOn(false);
    CandidateFontSizeCombo().SelectedIndex(3);
    CandidateScaleToggle().IsOn(true);
    UpdateAppearanceControlStates();
    UpdateColorSwatches();
  });
}

void MainWindow::ShowSettingsPage(std::wstring_view page) {
  const auto collapsed = Microsoft::UI::Xaml::Visibility::Collapsed;
  CommonPage().Visibility(collapsed);
  CorrectionPage().Visibility(collapsed);
  FuzzyPage().Visibility(collapsed);
  PunctuationPage().Visibility(collapsed);
  AppearancePage().Visibility(collapsed);
  DictionaryPage().Visibility(collapsed);
  KeysPage().Visibility(collapsed);
  AdvancedPage().Visibility(collapsed);

  const auto visible = Microsoft::UI::Xaml::Visibility::Visible;
  if (page == L"appearance") {
    AppearancePage().Visibility(visible);
  } else if (page == L"dictionary") {
    DictionaryPage().Visibility(visible);
  } else if (page == L"keys") {
    KeysPage().Visibility(visible);
  } else if (page == L"advanced") {
    AdvancedPage().Visibility(visible);
  } else if (page == L"correction") {
    CorrectionPage().Visibility(visible);
  } else if (page == L"fuzzy") {
    FuzzyPage().Visibility(visible);
  } else if (page == L"punctuation") {
    PunctuationPage().Visibility(visible);
  } else {
    CommonPage().Visibility(visible);
  }
}

void MainWindow::ApplyThemeFromControls() {
  Microsoft::UI::Xaml::ElementTheme theme = Microsoft::UI::Xaml::ElementTheme::Default;
  if (ThemeCombo().SelectedIndex() == 1) {
    theme = Microsoft::UI::Xaml::ElementTheme::Light;
  } else if (ThemeCombo().SelectedIndex() == 2) {
    theme = Microsoft::UI::Xaml::ElementTheme::Dark;
  }
  RootGrid().RequestedTheme(theme);
}

void MainWindow::UpdateAppearanceControlStates() {
  const bool colors_enabled = CustomColorsToggle().IsOn();
  CandidateColorControls().IsHitTestVisible(colors_enabled);
  CandidateColorControls().Opacity(colors_enabled ? 1.0 : 0.45);
  CandidateChineseFontCombo().IsEnabled(CustomFontsToggle().IsOn());
  CandidateEnglishFontCombo().IsEnabled(CustomFontsToggle().IsOn());
  CandidateFontSizeCombo().IsEnabled(CustomFontSizeToggle().IsOn());
}

void MainWindow::UpdateColorSwatches() {
  PreeditColorSwatch().Fill(
      Microsoft::UI::Xaml::Media::SolidColorBrush(PreeditColorPicker().Color()));
  HighlightedColorSwatch().Fill(
      Microsoft::UI::Xaml::Media::SolidColorBrush(HighlightedColorPicker().Color()));
  CandidateTextColorSwatch().Fill(
      Microsoft::UI::Xaml::Media::SolidColorBrush(CandidateTextColorPicker().Color()));
  BackgroundColorSwatch().Fill(
      Microsoft::UI::Xaml::Media::SolidColorBrush(BackgroundColorPicker().Color()));
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
  settings_.character_set = CharacterSetCombo().SelectedIndex() == 1
                                ? ziliu::core::CharacterSet::kTraditional
                                : ziliu::core::CharacterSet::kSimplified;
  settings_.punctuation_style = PunctuationCombo().SelectedIndex() == 0
                                    ? ziliu::core::PunctuationStyle::kHalfWidth
                                    : ziliu::core::PunctuationStyle::kFullWidth;
  settings_.default_input_mode = DefaultInputModeCombo().SelectedIndex() == 1
                                     ? ziliu::core::DefaultInputMode::kEnglish
                                     : ziliu::core::DefaultInputMode::kChinese;
  settings_.initialism_spelling = InitialismToggle().IsOn();
  settings_.spelling_correction = SpellingCorrectionToggle().IsOn();
  settings_.auto_pair_punctuation = AutoPairToggle().IsOn();
  settings_.smart_numeric_punctuation = SmartNumericPunctuationToggle().IsOn();
  const auto is_checked = [](Microsoft::UI::Xaml::Controls::CheckBox const& control) {
    const auto checked = control.IsChecked();
    return checked != nullptr && checked.Value();
  };
  settings_.correction_gn_ng = is_checked(CorrectionGnNgCheck());
  settings_.correction_mg_ng = is_checked(CorrectionMgNgCheck());
  settings_.correction_iou_iu = is_checked(CorrectionIouIuCheck());
  settings_.correction_uei_ui = is_checked(CorrectionUeiUiCheck());
  settings_.correction_uen_un = is_checked(CorrectionUenUnCheck());
  settings_.fuzzy_z_zh = is_checked(FuzzyZZhCheck());
  settings_.fuzzy_c_ch = is_checked(FuzzyCChCheck());
  settings_.fuzzy_s_sh = is_checked(FuzzySShCheck());
  settings_.fuzzy_l_n = is_checked(FuzzyLNCheck());
  settings_.fuzzy_f_h = is_checked(FuzzyFHCheck());
  settings_.fuzzy_r_l = is_checked(FuzzyRLCheck());
  settings_.fuzzy_an_ang = is_checked(FuzzyAnAngCheck());
  settings_.fuzzy_en_eng = is_checked(FuzzyEnEngCheck());
  settings_.fuzzy_in_ing = is_checked(FuzzyInIngCheck());
  settings_.fuzzy_ian_iang = is_checked(FuzzyIanIangCheck());
  settings_.fuzzy_uan_uang = is_checked(FuzzyUanUangCheck());
  settings_.theme_mode = static_cast<ziliu::core::ThemeMode>(ThemeCombo().SelectedIndex());
  settings_.candidate_layout = LayoutCombo().SelectedIndex() == 0
                                   ? ziliu::core::CandidateLayout::kHorizontal
                                   : ziliu::core::CandidateLayout::kVertical;
  settings_.candidate_count = static_cast<std::size_t>(CandidateCountCombo().SelectedIndex() + 3);
  settings_.candidate_page_mode = CandidatePageModeCombo().SelectedIndex() == 1
                                      ? ziliu::core::CandidatePageMode::kMultiLine
                                      : ziliu::core::CandidatePageMode::kSingleLine;
  settings_.custom_candidate_colors = CustomColorsToggle().IsOn();
  settings_.preedit_color = FromColor(PreeditColorPicker().Color());
  settings_.highlighted_candidate_color = FromColor(HighlightedColorPicker().Color());
  settings_.candidate_text_color = FromColor(CandidateTextColorPicker().Color());
  settings_.candidate_background_color = FromColor(BackgroundColorPicker().Color());
  settings_.custom_candidate_fonts = CustomFontsToggle().IsOn();
  settings_.candidate_chinese_font_family =
      static_cast<ziliu::core::CandidateChineseFontFamily>(
          CandidateChineseFontCombo().SelectedIndex());
  settings_.candidate_english_font_family =
      static_cast<ziliu::core::CandidateEnglishFontFamily>(
          CandidateEnglishFontCombo().SelectedIndex());
  settings_.custom_candidate_font_size = CustomFontSizeToggle().IsOn();
  settings_.candidate_font_size =
      static_cast<std::size_t>(CandidateFontSizeCombo().SelectedIndex() + 14);
  settings_.candidate_scale_with_text = CandidateScaleToggle().IsOn();
  settings_.input_mode_switch_key =
      SwitchKeyCombo().SelectedIndex() == 1 ? ziliu::core::InputModeSwitchKey::kControl
                                            : ziliu::core::InputModeSwitchKey::kShift;
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
