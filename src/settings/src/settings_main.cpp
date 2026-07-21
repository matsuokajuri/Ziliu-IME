#include "ziliu/core/settings.h"

#include <microsoft.ui.xaml.window.h>
#include <shellapi.h>
#include <windows.h>

#undef GetCurrentTime

#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/base.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>

namespace {

using namespace winrt;
using namespace Microsoft::UI;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Markup;
using namespace Microsoft::UI::Xaml::Media;

std::filesystem::path ExecutableDirectory() {
  std::wstring executable(32768, L'\0');
  const DWORD length =
      GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
  if (length == 0 || static_cast<std::size_t>(length) >= executable.size()) {
    return {};
  }
  executable.resize(length);
  return std::filesystem::path(executable).parent_path();
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

FrameworkElement LoadXamlView(std::wstring_view file_name) {
  const std::filesystem::path path = ExecutableDirectory() / L"Views" / file_name;
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    throw hresult_error(HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND), L"无法加载 WinUI 3 XAML 界面。");
  }
  const std::string markup{std::istreambuf_iterator<char>(stream),
                           std::istreambuf_iterator<char>()};
  return XamlReader::Load(to_hstring(markup)).as<FrameworkElement>();
}

template <typename T>
T FindViewElement(FrameworkElement const& root, std::wstring_view name) {
  const T element = root.FindName(name).try_as<T>();
  if (!element) {
    throw hresult_error(E_FAIL, L"WinUI 3 XAML 界面缺少必需控件。");
  }
  return element;
}

ComboBox CreateFallbackCombo(std::wstring_view header,
                             std::initializer_list<std::wstring_view> items) {
  ComboBox combo;
  combo.Header(box_value(header));
  combo.HorizontalAlignment(HorizontalAlignment::Stretch);
  for (const std::wstring_view item_text : items) {
    ComboBoxItem item;
    item.Content(box_value(item_text));
    combo.Items().Append(item);
  }
  return combo;
}

TextBlock CreateFallbackHeading(std::wstring_view text, double font_size) {
  TextBlock heading;
  heading.Text(text);
  heading.FontSize(font_size);
  return heading;
}

struct LaunchOptions {
  bool quick_menu = false;
  int anchor_x = 0;
  int anchor_y = 0;
};

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

class ZiliuSettingsApp : public ApplicationT<ZiliuSettingsApp> {
 public:
  explicit ZiliuSettingsApp(LaunchOptions options) : options_(options) {}

  void OnLaunched(LaunchActivatedEventArgs const&) {
    window_ = Window();
    window_.Title(L"字流 Ziliu 设置");
    if (options_.quick_menu) {
      BuildQuickMenuFallback();
    } else {
      BuildSettingsWindowFallback();
    }
    window_.Activate();
    try {
      ConfigureNativeWindow();
    } catch (...) {
      // Native popup sizing is optional; an activated WinUI window is still usable without it.
    }
  }

 private:
  void ConfigureNativeWindow() {
    HWND window_handle = nullptr;
    check_hresult(window_.as<::IWindowNative>()->get_WindowHandle(&window_handle));
    const UINT dpi = GetDpiForWindow(window_handle);
    const auto scaled = [dpi](int value) {
      return MulDiv(value, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI);
    };
    if (options_.quick_menu) {
      const int width = scaled(348);
      const int height = scaled(272);
      LONG_PTR style = GetWindowLongPtrW(window_handle, GWL_STYLE);
      style &= ~(static_cast<LONG_PTR>(WS_CAPTION) | static_cast<LONG_PTR>(WS_THICKFRAME) |
                 static_cast<LONG_PTR>(WS_MINIMIZEBOX) |
                 static_cast<LONG_PTR>(WS_MAXIMIZEBOX) | static_cast<LONG_PTR>(WS_SYSMENU));
      style |= WS_POPUP;
      SetWindowLongPtrW(window_handle, GWL_STYLE, style);

      POINT anchor{options_.anchor_x, options_.anchor_y};
      const HMONITOR monitor = MonitorFromPoint(anchor, MONITOR_DEFAULTTONEAREST);
      MONITORINFO monitor_info{sizeof(monitor_info)};
      GetMonitorInfoW(monitor, &monitor_info);
      const int work_left = static_cast<int>(monitor_info.rcWork.left);
      const int work_top = static_cast<int>(monitor_info.rcWork.top);
      const int work_right = static_cast<int>(monitor_info.rcWork.right);
      const int work_bottom = static_cast<int>(monitor_info.rcWork.bottom);
      const int x =
          std::clamp(options_.anchor_x - width / 2, work_left, work_right - width);
      const int preferred_y = options_.anchor_y - height - 8;
      const int y = preferred_y >= work_top
                        ? preferred_y
                        : std::min(options_.anchor_y + 36, work_bottom - height);
      SetWindowPos(window_handle, HWND_TOP, x, y, width, height,
                   SWP_FRAMECHANGED | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    } else {
      SetWindowPos(window_handle, nullptr, 0, 0, scaled(860), scaled(780),
                   SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
  }

  void BuildSettingsWindow() {
    settings_ = LoadSettings();
    const FrameworkElement view = LoadXamlView(L"SettingsView.xaml");
    layout_combo_ = FindViewElement<ComboBox>(view, L"LayoutCombo");
    candidate_count_ = FindViewElement<ComboBox>(view, L"CandidateCountCombo");
    switch_key_combo_ = FindViewElement<ComboBox>(view, L"SwitchKeyCombo");
    punctuation_toggle_ = FindViewElement<ToggleSwitch>(view, L"PunctuationToggle");
    auto_pair_punctuation_toggle_ = FindViewElement<ToggleSwitch>(view, L"AutoPairToggle");
    page_key_combo_ = FindViewElement<ComboBox>(view, L"PageKeyCombo");
    save_status_ = FindViewElement<InfoBar>(view, L"SaveStatus");
    const Button save_button = FindViewElement<Button>(view, L"SaveButton");

    layout_combo_.SelectedIndex(settings_.candidate_layout ==
                                        ziliu::core::CandidateLayout::kHorizontal
                                    ? 1
                                    : 0);
    candidate_count_.SelectedIndex(static_cast<int>(settings_.candidate_count) - 3);
    switch_key_combo_.SelectedIndex(
        settings_.input_mode_switch_key == ziliu::core::InputModeSwitchKey::kControl ? 1 : 0);
    punctuation_toggle_.IsOn(settings_.punctuation_style ==
                             ziliu::core::PunctuationStyle::kFullWidth);
    auto_pair_punctuation_toggle_.IsOn(settings_.auto_pair_punctuation);
    int page_key_index = 0;
    if (settings_.page_key_set == ziliu::core::PageKeySet::kSemicolonApostrophe) {
      page_key_index = 1;
    } else if (settings_.page_key_set == ziliu::core::PageKeySet::kBrackets) {
      page_key_index = 2;
    }
    page_key_combo_.SelectedIndex(page_key_index);
    save_button.Click(
        [this](IInspectable const&, RoutedEventArgs const&) { SaveFromControls(); });
    window_.Content(view);
  }

  void BuildQuickMenu() {
    settings_ = LoadSettings();
    const FrameworkElement view = LoadXamlView(L"QuickMenuView.xaml");
    character_set_toggle_ = FindViewElement<ToggleSwitch>(view, L"CharacterSetToggle");
    const Button open_settings = FindViewElement<Button>(view, L"OpenSettingsButton");
    character_set_toggle_.IsOn(settings_.character_set ==
                               ziliu::core::CharacterSet::kTraditional);
    character_set_toggle_.Toggled([this](IInspectable const&, RoutedEventArgs const&) {
      settings_.character_set = character_set_toggle_.IsOn()
                                    ? ziliu::core::CharacterSet::kTraditional
                                    : ziliu::core::CharacterSet::kSimplified;
      static_cast<void>(SaveSettings(settings_));
    });
    open_settings.Click([this](IInspectable const&, RoutedEventArgs const&) {
      const std::filesystem::path executable = ExecutableDirectory() / L"ZiliuSettings.exe";
      ShellExecuteW(nullptr, L"open", executable.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
      window_.Close();
    });
    window_.Content(view);
  }

  void BuildQuickMenuFallback() {
    settings_ = LoadSettings();

    StackPanel content;
    content.Padding(Thickness{22.0, 20.0, 22.0, 20.0});
    content.Spacing(14.0);
    content.Children().Append(CreateFallbackHeading(L"字流 Ziliu", 22.0));

    TextBlock hint;
    hint.Text(L"左键切换中英文 · 右键打开菜单");
    hint.Opacity(0.66);
    content.Children().Append(hint);

    character_set_toggle_ = ToggleSwitch();
    character_set_toggle_.Header(box_value(L"简繁转换"));
    character_set_toggle_.OnContent(box_value(L"繁体"));
    character_set_toggle_.OffContent(box_value(L"简体"));
    character_set_toggle_.IsOn(settings_.character_set ==
                               ziliu::core::CharacterSet::kTraditional);
    character_set_toggle_.Toggled([this](IInspectable const&, RoutedEventArgs const&) {
      settings_.character_set = character_set_toggle_.IsOn()
                                    ? ziliu::core::CharacterSet::kTraditional
                                    : ziliu::core::CharacterSet::kSimplified;
      static_cast<void>(SaveSettings(settings_));
    });
    content.Children().Append(character_set_toggle_);

    Button open_settings;
    open_settings.Content(box_value(L"打开完整设置"));
    open_settings.HorizontalAlignment(HorizontalAlignment::Stretch);
    open_settings.HorizontalContentAlignment(HorizontalAlignment::Center);
    open_settings.Click([this](IInspectable const&, RoutedEventArgs const&) {
      const std::filesystem::path executable = ExecutableDirectory() / L"ZiliuSettings.exe";
      ShellExecuteW(nullptr, L"open", executable.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
      window_.Close();
    });
    content.Children().Append(open_settings);
    window_.Content(content);
  }

  void BuildSettingsWindowFallback() {
    settings_ = LoadSettings();

    StackPanel content;
    content.Padding(Thickness{40.0, 32.0, 40.0, 40.0});
    content.Spacing(16.0);
    content.Children().Append(CreateFallbackHeading(L"字流设置", 28.0));

    TextBlock hint;
    hint.Text(L"调整候选窗口、输入行为和翻页方式");
    hint.Opacity(0.66);
    content.Children().Append(hint);

    layout_combo_ = CreateFallbackCombo(L"候选词排列", {L"竖排", L"横排"});
    layout_combo_.SelectedIndex(settings_.candidate_layout ==
                                        ziliu::core::CandidateLayout::kHorizontal
                                    ? 1
                                    : 0);
    content.Children().Append(layout_combo_);

    candidate_count_ =
        CreateFallbackCombo(L"每页候选词数量", {L"3", L"4", L"5", L"6", L"7", L"8", L"9"});
    candidate_count_.SelectedIndex(static_cast<int>(settings_.candidate_count) - 3);
    content.Children().Append(candidate_count_);

    switch_key_combo_ = CreateFallbackCombo(L"中英文切换按键", {L"Shift", L"Ctrl"});
    switch_key_combo_.SelectedIndex(
        settings_.input_mode_switch_key == ziliu::core::InputModeSwitchKey::kControl ? 1 : 0);
    content.Children().Append(switch_key_combo_);

    punctuation_toggle_ = ToggleSwitch();
    punctuation_toggle_.Header(box_value(L"中文标点"));
    punctuation_toggle_.OnContent(box_value(L"全角"));
    punctuation_toggle_.OffContent(box_value(L"半角"));
    punctuation_toggle_.IsOn(settings_.punctuation_style ==
                             ziliu::core::PunctuationStyle::kFullWidth);
    content.Children().Append(punctuation_toggle_);

    auto_pair_punctuation_toggle_ = ToggleSwitch();
    auto_pair_punctuation_toggle_.Header(box_value(L"自动补全成对符号"));
    auto_pair_punctuation_toggle_.OnContent(box_value(L"开启"));
    auto_pair_punctuation_toggle_.OffContent(box_value(L"关闭"));
    auto_pair_punctuation_toggle_.IsOn(settings_.auto_pair_punctuation);
    content.Children().Append(auto_pair_punctuation_toggle_);

    page_key_combo_ = CreateFallbackCombo(L"上一页 / 下一页", {L"， / 。", L"； / ‘", L"【 / 】"});
    int page_key_index = 0;
    if (settings_.page_key_set == ziliu::core::PageKeySet::kSemicolonApostrophe) {
      page_key_index = 1;
    } else if (settings_.page_key_set == ziliu::core::PageKeySet::kBrackets) {
      page_key_index = 2;
    }
    page_key_combo_.SelectedIndex(page_key_index);
    content.Children().Append(page_key_combo_);

    save_status_ = InfoBar();
    save_status_.IsOpen(false);
    save_status_.IsClosable(true);
    content.Children().Append(save_status_);

    Button save_button;
    save_button.Content(box_value(L"保存设置"));
    save_button.HorizontalAlignment(HorizontalAlignment::Left);
    save_button.Click(
        [this](IInspectable const&, RoutedEventArgs const&) { SaveFromControls(); });
    content.Children().Append(save_button);

    ScrollViewer scroll;
    scroll.Content(content);
    window_.Content(scroll);
  }

  void SaveFromControls() {
    settings_.candidate_layout = layout_combo_.SelectedIndex() == 1
                                     ? ziliu::core::CandidateLayout::kHorizontal
                                     : ziliu::core::CandidateLayout::kVertical;
    settings_.candidate_count = static_cast<std::size_t>(candidate_count_.SelectedIndex() + 3);
    settings_.input_mode_switch_key = switch_key_combo_.SelectedIndex() == 1
                                          ? ziliu::core::InputModeSwitchKey::kControl
                                          : ziliu::core::InputModeSwitchKey::kShift;
    settings_.punctuation_style = punctuation_toggle_.IsOn()
                                      ? ziliu::core::PunctuationStyle::kFullWidth
                                      : ziliu::core::PunctuationStyle::kHalfWidth;
    settings_.auto_pair_punctuation = auto_pair_punctuation_toggle_.IsOn();
    if (page_key_combo_.SelectedIndex() == 1) {
      settings_.page_key_set = ziliu::core::PageKeySet::kSemicolonApostrophe;
    } else if (page_key_combo_.SelectedIndex() == 2) {
      settings_.page_key_set = ziliu::core::PageKeySet::kBrackets;
    } else {
      settings_.page_key_set = ziliu::core::PageKeySet::kCommaPeriod;
    }
    const bool saved = SaveSettings(settings_);
    save_status_.Severity(saved ? InfoBarSeverity::Success : InfoBarSeverity::Error);
    save_status_.Title(saved ? L"设置已保存" : L"保存失败");
    save_status_.Message(saved ? L"新输入会立即使用这些设置。"
                               : L"请检查本地配置目录权限后重试。");
    save_status_.IsOpen(true);
  }

  LaunchOptions options_;
  Window window_{nullptr};
  ziliu::core::Settings settings_;
  ComboBox layout_combo_{nullptr};
  ComboBox candidate_count_{nullptr};
  ComboBox switch_key_combo_{nullptr};
  ToggleSwitch punctuation_toggle_{nullptr};
  ToggleSwitch auto_pair_punctuation_toggle_{nullptr};
  ComboBox page_key_combo_{nullptr};
  ToggleSwitch character_set_toggle_{nullptr};
  InfoBar save_status_{nullptr};
};

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous_instance, PWSTR command_line,
                    int show_command) {
  static_cast<void>(instance);
  static_cast<void>(previous_instance);
  static_cast<void>(command_line);
  static_cast<void>(show_command);
  const LaunchOptions options = ParseLaunchOptions();
  com_ptr<ZiliuSettingsApp> app;
  Application::Start([options, &app](auto&&) { app = make_self<ZiliuSettingsApp>(options); });
  return 0;
}
