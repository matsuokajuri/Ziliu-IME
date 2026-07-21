#include "ziliu/core/settings.h"

#include <microsoft.ui.xaml.window.h>
#include <shellapi.h>
#include <windows.h>

#undef GetCurrentTime

#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
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

TextBlock CreateLabel(std::wstring_view text) {
  TextBlock label;
  label.Text(text);
  label.FontSize(14.0);
  label.Margin(Thickness{0.0, 16.0, 0.0, 6.0});
  return label;
}

ComboBox CreateCombo(std::initializer_list<std::wstring_view> items, int selected_index) {
  ComboBox combo;
  combo.HorizontalAlignment(HorizontalAlignment::Stretch);
  for (const std::wstring_view item_text : items) {
    ComboBoxItem item;
    item.Content(box_value(item_text));
    combo.Items().Append(item);
  }
  combo.SelectedIndex(selected_index);
  return combo;
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
  explicit ZiliuSettingsApp(LaunchOptions options) : options_(options) {
  }

  void OnLaunched(LaunchActivatedEventArgs const&) {
    window_ = Window();
    window_.Title(L"字流 Ziliu 设置");
    if (options_.quick_menu) {
      BuildQuickMenu();
    } else {
      BuildSettingsWindow();
    }
    window_.Activate();
    ConfigureNativeWindow();
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
      const int width = scaled(286);
      const int height = scaled(190);
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
      SetWindowPos(window_handle, nullptr, 0, 0, scaled(760), scaled(720),
                   SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
  }

  void BuildSettingsWindow() {
    settings_ = LoadSettings();

    StackPanel content;
    content.Padding(Thickness{36.0, 30.0, 36.0, 36.0});
    content.Spacing(2.0);

    TextBlock title;
    title.Text(L"字流 Ziliu");
    title.FontSize(30.0);
    content.Children().Append(title);

    TextBlock subtitle;
    subtitle.Text(L"纯粹、轻量的中文输入体验");
    subtitle.Opacity(0.68);
    subtitle.Margin(Thickness{0.0, 4.0, 0.0, 18.0});
    content.Children().Append(subtitle);

    content.Children().Append(CreateLabel(L"候选词排列"));
    layout_combo_ = CreateCombo({L"竖排", L"横排"},
                                settings_.candidate_layout ==
                                        ziliu::core::CandidateLayout::kHorizontal
                                    ? 1
                                    : 0);
    content.Children().Append(layout_combo_);

    content.Children().Append(CreateLabel(L"每页候选词数量"));
    candidate_count_ = CreateCombo({L"3", L"4", L"5", L"6", L"7", L"8", L"9"},
                                   static_cast<int>(settings_.candidate_count) - 3);
    content.Children().Append(candidate_count_);

    content.Children().Append(CreateLabel(L"中英文切换按键"));
    switch_key_combo_ = CreateCombo(
        {L"Shift", L"Ctrl"},
        settings_.input_mode_switch_key == ziliu::core::InputModeSwitchKey::kControl ? 1 : 0);
    content.Children().Append(switch_key_combo_);

    punctuation_toggle_ = ToggleSwitch();
    punctuation_toggle_.Header(box_value(L"中文模式使用全角标点"));
    punctuation_toggle_.OnContent(box_value(L"全角"));
    punctuation_toggle_.OffContent(box_value(L"半角"));
    punctuation_toggle_.IsOn(settings_.punctuation_style ==
                             ziliu::core::PunctuationStyle::kFullWidth);
    punctuation_toggle_.Margin(Thickness{0.0, 20.0, 0.0, 4.0});
    content.Children().Append(punctuation_toggle_);

    auto_pair_punctuation_toggle_ = ToggleSwitch();
    auto_pair_punctuation_toggle_.Header(box_value(L"自动补全成对符号"));
    auto_pair_punctuation_toggle_.OnContent(box_value(L"开启"));
    auto_pair_punctuation_toggle_.OffContent(box_value(L"关闭"));
    auto_pair_punctuation_toggle_.IsOn(settings_.auto_pair_punctuation);
    auto_pair_punctuation_toggle_.Margin(Thickness{0.0, 12.0, 0.0, 4.0});
    content.Children().Append(auto_pair_punctuation_toggle_);

    content.Children().Append(CreateLabel(L"候选词翻页按键"));
    int page_key_index = 0;
    if (settings_.page_key_set == ziliu::core::PageKeySet::kSemicolonApostrophe) {
      page_key_index = 1;
    } else if (settings_.page_key_set == ziliu::core::PageKeySet::kBrackets) {
      page_key_index = 2;
    }
    page_key_combo_ = CreateCombo({L"， / 。", L"； / ‘", L"【 / 】"}, page_key_index);
    content.Children().Append(page_key_combo_);

    Button save_button;
    save_button.Content(box_value(L"保存设置"));
    save_button.HorizontalAlignment(HorizontalAlignment::Left);
    save_button.Margin(Thickness{0.0, 28.0, 0.0, 0.0});
    save_button.Click([this](IInspectable const&, RoutedEventArgs const&) { SaveFromControls(); });
    content.Children().Append(save_button);

    save_status_ = TextBlock();
    save_status_.Opacity(0.72);
    save_status_.Margin(Thickness{0.0, 12.0, 0.0, 0.0});
    content.Children().Append(save_status_);

    ScrollViewer scroll;
    scroll.Content(content);
    window_.Content(scroll);
  }

  void BuildQuickMenu() {
    settings_ = LoadSettings();

    StackPanel content;
    content.Padding(Thickness{20.0, 18.0, 20.0, 18.0});
    content.Spacing(12.0);

    TextBlock title;
    title.Text(L"字流输入法");
    title.FontSize(18.0);
    content.Children().Append(title);

    character_set_toggle_ = ToggleSwitch();
    character_set_toggle_.Header(box_value(L"简繁转换"));
    character_set_toggle_.OffContent(box_value(L"简体"));
    character_set_toggle_.OnContent(box_value(L"繁体"));
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
    open_settings.Content(box_value(L"打开设置"));
    open_settings.HorizontalAlignment(HorizontalAlignment::Stretch);
    open_settings.Click([this](IInspectable const&, RoutedEventArgs const&) {
      std::wstring executable(32768, L'\0');
      const DWORD length =
          GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
      if (length > 0 && static_cast<std::size_t>(length) < executable.size()) {
        executable.resize(length);
        ShellExecuteW(nullptr, L"open", executable.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
      }
      window_.Close();
    });
    content.Children().Append(open_settings);

    window_.Content(content);
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
    save_status_.Text(SaveSettings(settings_) ? L"已保存，新输入会立即使用这些设置。"
                                              : L"保存失败，请检查本地配置目录权限。");
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
  TextBlock save_status_{nullptr};
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
  Application::Start([options, &app](auto&&) {
    app = make_self<ZiliuSettingsApp>(options);
  });
  return 0;
}
