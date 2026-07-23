#include "pch.h"

#include "MainWindow.xaml.h"

#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"

#include "ziliu/core/settings.h"

#include <dwmapi.h>
#include <dwrite.h>
#include <microsoft.ui.xaml.window.h>
#include <shellapi.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace winrt::ZiliuSettings::implementation {
namespace {

HWND g_quick_menu_window = nullptr;
HHOOK g_quick_menu_mouse_hook = nullptr;
bool g_quick_menu_outside_click_armed = false;

LRESULT CALLBACK QuickMenuMouseHookProcedure(int code, WPARAM wparam, LPARAM lparam) {
  const bool is_mouse_button_down =
      wparam == WM_LBUTTONDOWN || wparam == WM_RBUTTONDOWN || wparam == WM_MBUTTONDOWN ||
      wparam == WM_XBUTTONDOWN;
  if (code == HC_ACTION && is_mouse_button_down && g_quick_menu_outside_click_armed &&
      g_quick_menu_window != nullptr) {
    const auto* mouse = reinterpret_cast<const MSLLHOOKSTRUCT*>(lparam);
    RECT window_rectangle{};
    if (mouse != nullptr && GetWindowRect(g_quick_menu_window, &window_rectangle) &&
        !PtInRect(&window_rectangle, mouse->pt)) {
      g_quick_menu_outside_click_armed = false;
      static_cast<void>(PostMessageW(g_quick_menu_window, WM_CLOSE, 0, 0));
    }
  }
  return CallNextHookEx(g_quick_menu_mouse_hook, code, wparam, lparam);
}

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
  std::filesystem::path temporary_path = *path;
  temporary_path += L".tmp";
  std::ofstream stream(temporary_path, std::ios::binary | std::ios::trunc);
  if (!stream) {
    return false;
  }
  const std::string serialized = ziliu::core::SerializeSettings(settings);
  stream.write(serialized.data(), static_cast<std::streamsize>(serialized.size()));
  stream.flush();
  const bool write_succeeded = stream.good();
  stream.close();
  if (!write_succeeded || stream.fail()) {
    std::error_code remove_error;
    std::filesystem::remove(temporary_path, remove_error);
    return false;
  }
  if (!MoveFileExW(temporary_path.c_str(), path->c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    std::error_code remove_error;
    std::filesystem::remove(temporary_path, remove_error);
    return false;
  }
  return true;
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

bool UseDarkTheme(ziliu::core::ThemeMode mode) {
  if (mode == ziliu::core::ThemeMode::kDark) {
    return true;
  }
  if (mode == ziliu::core::ThemeMode::kLight) {
    return false;
  }
  DWORD use_light_theme = 1;
  DWORD size = sizeof(use_light_theme);
  const LSTATUS result = RegGetValueW(
      HKEY_CURRENT_USER,
      L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
      L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &use_light_theme, &size);
  return result == ERROR_SUCCESS && use_light_theme == 0;
}

std::optional<std::wstring> LocalizedFontFamilyName(IDWriteFontFamily* family) {
  ::Microsoft::WRL::ComPtr<IDWriteLocalizedStrings> names;
  if (family == nullptr || FAILED(family->GetFamilyNames(names.GetAddressOf())) ||
      names->GetCount() == 0) {
    return std::nullopt;
  }

  UINT32 name_index = 0;
  BOOL locale_exists = FALSE;
  wchar_t locale_name[LOCALE_NAME_MAX_LENGTH]{};
  if (GetUserDefaultLocaleName(locale_name, LOCALE_NAME_MAX_LENGTH) > 0) {
    static_cast<void>(names->FindLocaleName(locale_name, &name_index, &locale_exists));
  }
  if (locale_exists == FALSE) {
    static_cast<void>(names->FindLocaleName(L"en-us", &name_index, &locale_exists));
  }
  if (locale_exists == FALSE) {
    name_index = 0;
  }

  UINT32 name_length = 0;
  if (FAILED(names->GetStringLength(name_index, &name_length))) {
    return std::nullopt;
  }
  std::wstring name(static_cast<std::size_t>(name_length) + 1, L'\0');
  if (FAILED(names->GetString(name_index, name.data(), name_length + 1))) {
    return std::nullopt;
  }
  name.resize(name_length);
  return name.empty() ? std::nullopt : std::optional<std::wstring>(std::move(name));
}

bool FontFamilyLess(const std::wstring& left, const std::wstring& right) {
  const int comparison =
      CompareStringOrdinal(left.c_str(), -1, right.c_str(), -1, TRUE);
  return comparison == CSTR_LESS_THAN || (comparison == 0 && left < right);
}

bool FontFamilyEqual(const std::wstring& left, const std::wstring& right) {
  return CompareStringOrdinal(left.c_str(), -1, right.c_str(), -1, TRUE) == CSTR_EQUAL;
}

std::vector<std::wstring> EnumerateSystemFontFamilies() {
  std::vector<std::wstring> families;
  ::Microsoft::WRL::ComPtr<IDWriteFactory> factory;
  const HRESULT factory_result =
      DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                          reinterpret_cast<IUnknown**>(factory.GetAddressOf()));
  if (SUCCEEDED(factory_result)) {
    ::Microsoft::WRL::ComPtr<IDWriteFontCollection> collection;
    if (SUCCEEDED(factory->GetSystemFontCollection(collection.GetAddressOf(), FALSE))) {
      const UINT32 family_count = collection->GetFontFamilyCount();
      families.reserve(family_count);
      for (UINT32 index = 0; index < family_count; ++index) {
        ::Microsoft::WRL::ComPtr<IDWriteFontFamily> family;
        if (SUCCEEDED(collection->GetFontFamily(index, family.GetAddressOf()))) {
          const auto name = LocalizedFontFamilyName(family.Get());
          if (name.has_value()) {
            families.push_back(*name);
          }
        }
      }
    }
  }

  if (families.empty()) {
    families = {L"Microsoft YaHei UI", L"Segoe UI Variable Text", L"Arial", L"SimSun"};
  }
  std::sort(families.begin(), families.end(), FontFamilyLess);
  families.erase(std::unique(families.begin(), families.end(), FontFamilyEqual), families.end());
  return families;
}

int FindFontFamilyIndex(const std::vector<std::wstring>& families,
                        const std::wstring& requested_family) {
  for (std::size_t index = 0; index < families.size(); ++index) {
    if (FontFamilyEqual(families[index], requested_family)) {
      return static_cast<int>(index);
    }
  }
  return -1;
}

void PopulateFontComboBox(Microsoft::UI::Xaml::Controls::ComboBox const& combo_box,
                          const std::vector<std::wstring>& families,
                          const std::string& selected_family,
                          std::wstring_view fallback_family) {
  combo_box.Items().Clear();
  for (const auto& family : families) {
    combo_box.Items().Append(winrt::box_value(winrt::hstring(family)));
  }

  int selected_index =
      FindFontFamilyIndex(families, std::wstring(winrt::to_hstring(selected_family)));
  if (selected_index < 0) {
    selected_index = FindFontFamilyIndex(families, std::wstring(fallback_family));
  }
  if (selected_index < 0 && !families.empty()) {
    selected_index = 0;
  }
  combo_box.SelectedIndex(selected_index);
}

void SelectFontComboBox(Microsoft::UI::Xaml::Controls::ComboBox const& combo_box,
                        std::string_view requested_family,
                        std::wstring_view fallback_family) {
  const auto requested = winrt::to_hstring(std::string(requested_family));
  int fallback_index = -1;
  for (UINT32 index = 0; index < combo_box.Items().Size(); ++index) {
    const auto name = winrt::unbox_value_or<winrt::hstring>(
        combo_box.Items().GetAt(index), winrt::hstring{});
    if (CompareStringOrdinal(name.c_str(), -1, requested.c_str(), -1, TRUE) == CSTR_EQUAL) {
      combo_box.SelectedIndex(static_cast<int>(index));
      return;
    }
    if (fallback_index < 0 &&
        CompareStringOrdinal(name.c_str(), -1, fallback_family.data(),
                             static_cast<int>(fallback_family.size()), TRUE) == CSTR_EQUAL) {
      fallback_index = static_cast<int>(index);
    }
  }
  combo_box.SelectedIndex(fallback_index >= 0 ? fallback_index
                                              : (combo_box.Items().Size() > 0 ? 0 : -1));
}

std::string SelectedFontFamily(Microsoft::UI::Xaml::Controls::ComboBox const& combo_box,
                               const std::string& fallback_family) {
  const auto selected = combo_box.SelectedItem();
  if (selected == nullptr) {
    return fallback_family;
  }
  const auto name =
      winrt::unbox_value_or<winrt::hstring>(selected, winrt::hstring{});
  return name.empty() ? fallback_family : winrt::to_string(name);
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
    PrepareQuickMenuOpenAnimation();
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
    Microsoft::UI::Xaml::Window window = *this;
    winrt::check_hresult(window.as<::IWindowNative>()->get_WindowHandle(&g_quick_menu_window));
    g_quick_menu_outside_click_armed = false;
    g_quick_menu_mouse_hook =
        SetWindowsHookExW(WH_MOUSE_LL, QuickMenuMouseHookProcedure, GetModuleHandleW(nullptr), 0);
    Closed([](winrt::Windows::Foundation::IInspectable const&,
              Microsoft::UI::Xaml::WindowEventArgs const&) {
      g_quick_menu_outside_click_armed = false;
      g_quick_menu_window = nullptr;
      if (g_quick_menu_mouse_hook != nullptr) {
        UnhookWindowsHookEx(g_quick_menu_mouse_hook);
        g_quick_menu_mouse_hook = nullptr;
      }
    });
    quick_menu_close_arm_timer_ =
        Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread().CreateTimer();
    quick_menu_close_arm_timer_.Interval(std::chrono::milliseconds(600));
    quick_menu_close_arm_timer_.IsRepeating(false);
    quick_menu_close_arm_timer_.Tick([this](auto const&, auto const&) {
      g_quick_menu_outside_click_armed = true;
      quick_menu_close_arm_timer_ = nullptr;
    });
    quick_menu_close_arm_timer_.Start();
    Activated(
        [this](winrt::Windows::Foundation::IInspectable const&,
               Microsoft::UI::Xaml::WindowActivatedEventArgs const& args) {
          const auto activation_state = args.WindowActivationState();
          if (activation_state !=
                  Microsoft::UI::Xaml::WindowActivationState::Deactivated &&
              !quick_menu_animation_started_) {
            quick_menu_animation_started_ = true;
            PlayQuickMenuOpenAnimation();
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
  ChineseCandidatesOnlyToggle().IsOn(settings_.chinese_candidates_only);
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
  const auto system_font_families = EnumerateSystemFontFamilies();
  PopulateFontComboBox(CandidateChineseFontCombo(), system_font_families,
                       settings_.candidate_chinese_font_family, L"Microsoft YaHei UI");
  PopulateFontComboBox(CandidateEnglishFontCombo(), system_font_families,
                       settings_.candidate_english_font_family, L"Segoe UI Variable Text");
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
  UpdateCandidatePreview();
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
  ThemeSettingsButton().Click(
      [this](auto const&, auto const&) { ShowSettingsPage(L"theme"); });
  CorrectionBackButton().Click([this](auto const&, auto const&) { ShowSettingsPage(L"common"); });
  FuzzyBackButton().Click([this](auto const&, auto const&) { ShowSettingsPage(L"common"); });
  PunctuationBackButton().Click(
      [this](auto const&, auto const&) { ShowSettingsPage(L"common"); });
  ThemeBackButton().Click(
      [this](auto const&, auto const&) { ShowSettingsPage(L"appearance"); });
  ThemeCombo().SelectionChanged([this](auto const&, auto const&) {
    ApplyThemeFromControls();
    UpdateCandidatePreview();
  });
  const auto update_appearance_state = [this](auto const&, auto const&) {
    UpdateAppearanceControlStates();
    UpdateCandidatePreview();
  };
  CustomColorsToggle().Toggled(update_appearance_state);
  CustomFontsToggle().Toggled(update_appearance_state);
  CustomFontSizeToggle().Toggled(update_appearance_state);
  const auto update_preview = [this](auto const&, auto const&) { UpdateCandidatePreview(); };
  LayoutCombo().SelectionChanged(update_preview);
  CandidateCountCombo().SelectionChanged(update_preview);
  CandidatePageModeCombo().SelectionChanged(update_preview);
  CandidateChineseFontCombo().SelectionChanged(update_preview);
  CandidateEnglishFontCombo().SelectionChanged(update_preview);
  CandidateFontSizeCombo().SelectionChanged(update_preview);
  CandidateScaleToggle().Toggled(update_preview);
  const auto update_color = [this](auto const&, auto const&) {
    UpdateColorSwatches();
    UpdateCandidatePreview();
  };
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
    const ziliu::core::Settings defaults;
    SelectFontComboBox(CandidateChineseFontCombo(), defaults.candidate_chinese_font_family,
                       L"Microsoft YaHei UI");
    SelectFontComboBox(CandidateEnglishFontCombo(), defaults.candidate_english_font_family,
                       L"Segoe UI Variable Text");
    CustomFontSizeToggle().IsOn(false);
    CandidateFontSizeCombo().SelectedIndex(3);
    CandidateScaleToggle().IsOn(true);
    UpdateAppearanceControlStates();
    UpdateColorSwatches();
    UpdateCandidatePreview();
  });
}

void MainWindow::ShowSettingsPage(std::wstring_view page) {
  const auto collapsed = Microsoft::UI::Xaml::Visibility::Collapsed;
  CommonPage().Visibility(collapsed);
  CorrectionPage().Visibility(collapsed);
  FuzzyPage().Visibility(collapsed);
  PunctuationPage().Visibility(collapsed);
  AppearancePage().Visibility(collapsed);
  ThemePage().Visibility(collapsed);
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
  } else if (page == L"theme") {
    ThemePage().Visibility(visible);
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

void MainWindow::UpdateCandidatePreview() {
  const int theme_index = ThemeCombo().SelectedIndex();
  const int layout_index = LayoutCombo().SelectedIndex();
  const int candidate_count_index = CandidateCountCombo().SelectedIndex();
  if (theme_index < 0 || layout_index < 0 || candidate_count_index < 0) {
    return;
  }

  ziliu::core::Settings preview_settings = settings_;
  preview_settings.theme_mode = static_cast<ziliu::core::ThemeMode>(theme_index);
  preview_settings.candidate_layout = layout_index == 0
                                          ? ziliu::core::CandidateLayout::kHorizontal
                                          : ziliu::core::CandidateLayout::kVertical;
  preview_settings.candidate_count = std::clamp(
      static_cast<std::size_t>(candidate_count_index + 3),
      ziliu::core::kMinimumCandidateCount, ziliu::core::kMaximumCandidateCount);
  preview_settings.candidate_page_mode =
      CandidatePageModeCombo().SelectedIndex() == 1
          ? ziliu::core::CandidatePageMode::kMultiLine
          : ziliu::core::CandidatePageMode::kSingleLine;
  preview_settings.custom_candidate_colors = CustomColorsToggle().IsOn();
  preview_settings.preedit_color = FromColor(PreeditColorPicker().Color());
  preview_settings.highlighted_candidate_color =
      FromColor(HighlightedColorPicker().Color());
  preview_settings.candidate_text_color = FromColor(CandidateTextColorPicker().Color());
  preview_settings.candidate_background_color = FromColor(BackgroundColorPicker().Color());
  preview_settings.custom_candidate_fonts = CustomFontsToggle().IsOn();
  preview_settings.candidate_chinese_font_family =
      SelectedFontFamily(CandidateChineseFontCombo(),
                         preview_settings.candidate_chinese_font_family);
  preview_settings.candidate_english_font_family =
      SelectedFontFamily(CandidateEnglishFontCombo(),
                         preview_settings.candidate_english_font_family);
  preview_settings.custom_candidate_font_size = CustomFontSizeToggle().IsOn();
  if (CandidateFontSizeCombo().SelectedIndex() >= 0) {
    preview_settings.candidate_font_size = std::clamp(
        static_cast<std::size_t>(CandidateFontSizeCombo().SelectedIndex() + 14),
        ziliu::core::kMinimumCandidateFontSize,
        ziliu::core::kMaximumCandidateFontSize);
  }
  preview_settings.candidate_scale_with_text = CandidateScaleToggle().IsOn();

  const bool dark_theme = UseDarkTheme(preview_settings.theme_mode);
  const ziliu::core::CandidatePalette palette =
      ziliu::core::ResolveCandidatePalette(preview_settings, dark_theme);
  const auto make_brush = [](std::uint32_t color) {
    return Microsoft::UI::Xaml::Media::SolidColorBrush(ToColor(color));
  };
  const auto background_brush = make_brush(palette.candidate_background_color);
  const auto preedit_brush = make_brush(palette.preedit_color);
  const auto candidate_brush = make_brush(palette.candidate_text_color);
  const auto highlighted_brush = make_brush(palette.highlighted_candidate_color);
  const auto muted_brush = make_brush(palette.muted_color);
  const auto highlight_background_brush =
      make_brush(palette.highlight_background_color);

  const float font_size = static_cast<float>(
      preview_settings.custom_candidate_font_size
          ? preview_settings.candidate_font_size
          : 17);
  const float layout_scale =
      preview_settings.candidate_scale_with_text
          ? std::clamp(font_size / 17.0F, 0.82F, 1.42F)
          : 1.0F;
  const std::string chinese_family =
      preview_settings.custom_candidate_fonts
          ? preview_settings.candidate_chinese_font_family
          : "Source Han Sans SC";
  const std::string english_family =
      preview_settings.custom_candidate_fonts
          ? preview_settings.candidate_english_font_family
          : "Segoe UI Variable Text";
  const Microsoft::UI::Xaml::Media::FontFamily chinese_font(
      winrt::to_hstring(chinese_family));
  const Microsoft::UI::Xaml::Media::FontFamily english_font(
      winrt::to_hstring(english_family));

  CandidatePreviewContent().Children().Clear();
  CandidatePreviewWindow().Background(background_brush);
  CandidatePreviewWindow().BorderBrush(muted_brush);

  const bool horizontal =
      preview_settings.candidate_layout == ziliu::core::CandidateLayout::kHorizontal;
  const double preedit_height = (horizontal ? 34.0 : 42.0) * layout_scale;
  Microsoft::UI::Xaml::Controls::Border preedit_region;
  preedit_region.Height(preedit_height);
  preedit_region.Padding(
      Microsoft::UI::Xaml::Thickness{14.0 * layout_scale, 0.0,
                                     14.0 * layout_scale, 0.0});
  Microsoft::UI::Xaml::Controls::TextBlock preedit_text;
  preedit_text.Text(L"ziliu shurufa");
  preedit_text.VerticalAlignment(Microsoft::UI::Xaml::VerticalAlignment::Center);
  preedit_text.Foreground(preedit_brush);
  preedit_text.FontFamily(english_font);
  preedit_text.FontSize(font_size + 1.0F);
  preedit_text.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
  preedit_text.TextWrapping(Microsoft::UI::Xaml::TextWrapping::NoWrap);
  preedit_region.Child(preedit_text);
  CandidatePreviewContent().Children().Append(preedit_region);

  Microsoft::UI::Xaml::Controls::Border divider;
  divider.Height(0.5);
  divider.Margin(Microsoft::UI::Xaml::Thickness{
      14.0 * layout_scale, 0.0, 14.0 * layout_scale, 0.0});
  divider.Background(muted_brush);
  CandidatePreviewContent().Children().Append(divider);

  static constexpr std::array<std::wstring_view, 9> candidate_words{
      L"字流", L"输入法", L"简洁", L"高效", L"纯粹",
      L"中文", L"拼音", L"开源", L"轻巧"};
  const auto create_candidate =
      [&](std::size_t index, bool horizontal_candidate) {
        Microsoft::UI::Xaml::Controls::Border cell;
        cell.Height((horizontal_candidate ? 34.0 : 36.0) * layout_scale);
        cell.CornerRadius(Microsoft::UI::Xaml::CornerRadius{
            10.0, 10.0, 10.0, 10.0});
        cell.Padding(Microsoft::UI::Xaml::Thickness{
            (horizontal_candidate ? 8.0 : 6.0) * layout_scale, 0.0,
            6.0 * layout_scale, 0.0});
        if (index == 0) {
          cell.Background(highlight_background_brush);
        }

        Microsoft::UI::Xaml::Controls::TextBlock label;
        const std::wstring label_text =
            std::to_wstring(index + 1) + L"  " +
            std::wstring(candidate_words[index]);
        label.Text(winrt::hstring(label_text));
        label.VerticalAlignment(Microsoft::UI::Xaml::VerticalAlignment::Center);
        label.Foreground(index == 0 ? highlighted_brush : candidate_brush);
        label.FontFamily(chinese_font);
        label.FontSize(font_size);
        label.TextWrapping(Microsoft::UI::Xaml::TextWrapping::NoWrap);
        label.TextTrimming(Microsoft::UI::Xaml::TextTrimming::CharacterEllipsis);
        cell.Child(label);
        return cell;
      };

  if (horizontal) {
    const bool expandable =
        preview_settings.candidate_page_mode ==
        ziliu::core::CandidatePageMode::kMultiLine;
    const std::size_t row_count = 1;
    const std::size_t column_count = preview_settings.candidate_count;
    const double cell_width = 76.0 * layout_scale;
    const double action_width = (expandable ? 88.0 : 48.0) * layout_scale;
    CandidatePreviewWindow().Width(std::max(
        280.0 * layout_scale,
        16.0 * layout_scale + static_cast<double>(column_count) * cell_width +
            action_width));

    Microsoft::UI::Xaml::Controls::StackPanel rows;
    rows.Margin(Microsoft::UI::Xaml::Thickness{
        8.0 * layout_scale, 3.5 * layout_scale,
        8.0 * layout_scale, 4.0 * layout_scale});
    for (std::size_t row_index = 0; row_index < row_count; ++row_index) {
      Microsoft::UI::Xaml::Controls::StackPanel row;
      row.Orientation(Microsoft::UI::Xaml::Controls::Orientation::Horizontal);
      row.Height(36.0 * layout_scale);
      const std::size_t begin = row_index * column_count;
      const std::size_t end =
          std::min(begin + column_count, preview_settings.candidate_count);
      for (std::size_t index = begin; index < end; ++index) {
        auto cell = create_candidate(index, true);
        cell.Width(cell_width);
        cell.Margin(Microsoft::UI::Xaml::Thickness{
            0.0, 1.0 * layout_scale, 2.0 * layout_scale,
            1.0 * layout_scale});
        row.Children().Append(cell);
      }
      const auto append_action_button =
          [&](std::wstring_view glyph, double width) {
            Microsoft::UI::Xaml::Controls::Border button;
            button.Width(width * layout_scale);
            button.Height(36.0 * layout_scale);
            button.BorderBrush(muted_brush);
            button.BorderThickness(
                Microsoft::UI::Xaml::Thickness{0.5, 0.0, 0.0, 0.0});
            Microsoft::UI::Xaml::Controls::FontIcon icon;
            icon.Glyph(winrt::hstring(glyph));
            icon.FontSize(16.0 * layout_scale);
            icon.Foreground(candidate_brush);
            button.Child(icon);
            row.Children().Append(button);
          };
      if (expandable) {
        append_action_button(L"\uE70D", 40.0);
      }
      append_action_button(L"\uE700", 48.0);
      rows.Children().Append(row);
    }
    CandidatePreviewContent().Children().Append(rows);
  } else {
    CandidatePreviewWindow().Width(420.0 * layout_scale);
    Microsoft::UI::Xaml::Controls::StackPanel candidates;
    candidates.Margin(Microsoft::UI::Xaml::Thickness{
        8.0 * layout_scale, 13.5 * layout_scale,
        8.0 * layout_scale, 14.0 * layout_scale});
    for (std::size_t index = 0; index < preview_settings.candidate_count; ++index) {
      auto cell = create_candidate(index, false);
      cell.HorizontalAlignment(Microsoft::UI::Xaml::HorizontalAlignment::Stretch);
      cell.Margin(Microsoft::UI::Xaml::Thickness{
          0.0, 1.0 * layout_scale, 0.0, 1.0 * layout_scale});
      candidates.Children().Append(cell);
    }
    CandidatePreviewContent().Children().Append(candidates);
  }
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

void MainWindow::PrepareQuickMenuOpenAnimation() {
  const auto visual =
      Microsoft::UI::Xaml::Hosting::ElementCompositionPreview::GetElementVisual(RootGrid());
  visual.Opacity(0.0f);
  visual.Offset({0.0f, 12.0f, 0.0f});
}

void MainWindow::PlayQuickMenuOpenAnimation() {
  const auto visual =
      Microsoft::UI::Xaml::Hosting::ElementCompositionPreview::GetElementVisual(RootGrid());
  const auto compositor = visual.Compositor();
  const auto easing =
      compositor.CreateCubicBezierEasingFunction({0.1f, 0.9f}, {0.2f, 1.0f});

  const auto opacity_animation = compositor.CreateScalarKeyFrameAnimation();
  opacity_animation.InsertKeyFrame(0.0f, 0.0f);
  opacity_animation.InsertKeyFrame(1.0f, 1.0f, easing);
  opacity_animation.Duration(std::chrono::milliseconds(180));

  const auto offset_animation = compositor.CreateVector3KeyFrameAnimation();
  offset_animation.InsertKeyFrame(0.0f, {0.0f, 12.0f, 0.0f});
  offset_animation.InsertKeyFrame(1.0f, {0.0f, 0.0f, 0.0f}, easing);
  offset_animation.Duration(std::chrono::milliseconds(180));

  visual.Opacity(1.0f);
  visual.Offset({0.0f, 0.0f, 0.0f});
  visual.StartAnimation(L"Opacity", opacity_animation);
  visual.StartAnimation(L"Offset", offset_animation);
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
  settings_.chinese_candidates_only = ChineseCandidatesOnlyToggle().IsOn();
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
      SelectedFontFamily(CandidateChineseFontCombo(), settings_.candidate_chinese_font_family);
  settings_.candidate_english_font_family =
      SelectedFontFamily(CandidateEnglishFontCombo(), settings_.candidate_english_font_family);
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
