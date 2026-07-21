#include "ziliu/core/settings.h"

#include <microsoft.ui.xaml.window.h>
#include <shellapi.h>
#include <windows.h>

#undef GetCurrentTime

#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.Text.h>
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
using namespace Microsoft::UI::Xaml::Media;

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

template <typename T>
T ThemeResource(std::wstring_view key) {
  const auto resources = Application::Current().Resources();
  const auto boxed_key = box_value(hstring(key));
  if (!resources.HasKey(boxed_key)) {
    return nullptr;
  }
  return resources.Lookup(boxed_key).try_as<T>();
}

ComboBox CreateCombo(std::initializer_list<std::wstring_view> items, int selected_index) {
  ComboBox combo;
  combo.MinWidth(220.0);
  combo.HorizontalAlignment(HorizontalAlignment::Right);
  for (const std::wstring_view item_text : items) {
    ComboBoxItem item;
    item.Content(box_value(item_text));
    combo.Items().Append(item);
  }
  combo.SelectedIndex(selected_index);
  return combo;
}

TextBlock CreateSectionTitle(std::wstring_view text) {
  TextBlock title;
  title.Text(text);
  title.FontSize(20.0);
  title.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
  title.Margin(Thickness{0.0, 20.0, 0.0, 10.0});
  return title;
}

Border CreateCard() {
  Border card;
  card.CornerRadius(CornerRadius{10.0});
  card.BorderThickness(Thickness{1.0});
  if (const auto background = ThemeResource<Brush>(L"CardBackgroundFillColorDefaultBrush")) {
    card.Background(background);
  }
  if (const auto border = ThemeResource<Brush>(L"CardStrokeColorDefaultBrush")) {
    card.BorderBrush(border);
  }
  return card;
}

void AppendSettingsRow(StackPanel const& rows, std::wstring_view title_text,
                       std::wstring_view description_text, FrameworkElement const& control) {
  if (rows.Children().Size() != 0) {
    Border divider;
    divider.Height(1.0);
    divider.Margin(Thickness{18.0, 0.0, 18.0, 0.0});
    if (const auto brush = ThemeResource<Brush>(L"DividerStrokeColorDefaultBrush")) {
      divider.Background(brush);
    }
    rows.Children().Append(divider);
  }

  Grid row;
  ColumnDefinition text_column;
  text_column.Width(GridLength{1.0, GridUnitType::Star});
  row.ColumnDefinitions().Append(text_column);
  ColumnDefinition control_column;
  control_column.Width(GridLength{1.0, GridUnitType::Auto});
  row.ColumnDefinitions().Append(control_column);

  StackPanel labels;
  labels.Spacing(3.0);
  labels.VerticalAlignment(VerticalAlignment::Center);
  TextBlock title;
  title.Text(title_text);
  title.FontSize(15.0);
  title.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
  labels.Children().Append(title);
  TextBlock description;
  description.Text(description_text);
  description.FontSize(12.0);
  description.Opacity(0.66);
  description.TextWrapping(TextWrapping::Wrap);
  labels.Children().Append(description);
  row.Children().Append(labels);

  control.VerticalAlignment(VerticalAlignment::Center);
  control.Margin(Thickness{24.0, 0.0, 0.0, 0.0});
  Grid::SetColumn(control, 1);
  row.Children().Append(control);

  Border row_container;
  row_container.Padding(Thickness{18.0, 15.0, 18.0, 15.0});
  row_container.Child(row);
  rows.Children().Append(row_container);
}

void ApplyAccentStyle(Button const& button) {
  if (const auto style = ThemeResource<Style>(L"AccentButtonStyle")) {
    button.Style(style);
  }
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
    window_.SystemBackdrop(MicaBackdrop());
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

    Grid root;
    if (const auto background = ThemeResource<Brush>(L"LayerFillColorDefaultBrush")) {
      root.Background(background);
    }

    StackPanel content;
    content.Padding(Thickness{40.0, 32.0, 40.0, 40.0});
    content.MaxWidth(820.0);
    content.HorizontalAlignment(HorizontalAlignment::Center);

    Grid header;
    ColumnDefinition icon_column;
    icon_column.Width(GridLength{1.0, GridUnitType::Auto});
    header.ColumnDefinitions().Append(icon_column);
    ColumnDefinition title_column;
    title_column.Width(GridLength{1.0, GridUnitType::Star});
    header.ColumnDefinitions().Append(title_column);

    Border emblem;
    emblem.Width(52.0);
    emblem.Height(52.0);
    emblem.CornerRadius(CornerRadius{13.0});
    emblem.VerticalAlignment(VerticalAlignment::Center);
    if (const auto accent = ThemeResource<Brush>(L"AccentFillColorDefaultBrush")) {
      emblem.Background(accent);
    }
    FontIcon emblem_icon;
    emblem_icon.Glyph(L"\uE765");
    emblem_icon.FontSize(25.0);
    if (const auto foreground = ThemeResource<Brush>(L"TextOnAccentFillColorPrimaryBrush")) {
      emblem_icon.Foreground(foreground);
    }
    emblem.Child(emblem_icon);
    header.Children().Append(emblem);

    StackPanel heading;
    heading.Spacing(3.0);
    heading.Margin(Thickness{16.0, 0.0, 0.0, 0.0});
    Grid::SetColumn(heading, 1);

    TextBlock title;
    title.Text(L"字流设置");
    title.FontSize(28.0);
    title.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
    heading.Children().Append(title);

    TextBlock subtitle;
    subtitle.Text(L"调整候选窗口、输入行为和翻页方式");
    subtitle.FontSize(13.0);
    subtitle.Opacity(0.66);
    heading.Children().Append(subtitle);
    header.Children().Append(heading);
    content.Children().Append(header);

    content.Children().Append(CreateSectionTitle(L"候选窗口"));
    StackPanel candidate_rows;
    layout_combo_ = CreateCombo({L"竖排", L"横排"},
                                settings_.candidate_layout ==
                                        ziliu::core::CandidateLayout::kHorizontal
                                    ? 1
                                    : 0);
    AppendSettingsRow(candidate_rows, L"候选词排列", L"选择纵向列表或横向排列", layout_combo_);
    candidate_count_ = CreateCombo({L"3", L"4", L"5", L"6", L"7", L"8", L"9"},
                                   static_cast<int>(settings_.candidate_count) - 3);
    AppendSettingsRow(candidate_rows, L"每页候选词数量", L"控制候选窗口一次显示的词条数量",
                      candidate_count_);
    Border candidate_card = CreateCard();
    candidate_card.Child(candidate_rows);
    content.Children().Append(candidate_card);

    content.Children().Append(CreateSectionTitle(L"输入行为"));
    StackPanel input_rows;
    switch_key_combo_ = CreateCombo(
        {L"Shift", L"Ctrl"},
        settings_.input_mode_switch_key == ziliu::core::InputModeSwitchKey::kControl ? 1 : 0);
    AppendSettingsRow(input_rows, L"中英文切换按键", L"单独按下该按键时切换输入模式",
                      switch_key_combo_);

    punctuation_toggle_ = ToggleSwitch();
    punctuation_toggle_.OnContent(box_value(L"全角"));
    punctuation_toggle_.OffContent(box_value(L"半角"));
    punctuation_toggle_.IsOn(settings_.punctuation_style ==
                             ziliu::core::PunctuationStyle::kFullWidth);
    punctuation_toggle_.MinWidth(120.0);
    AppendSettingsRow(input_rows, L"中文标点", L"选择中文模式下使用全角或半角标点",
                      punctuation_toggle_);

    auto_pair_punctuation_toggle_ = ToggleSwitch();
    auto_pair_punctuation_toggle_.OnContent(box_value(L"开启"));
    auto_pair_punctuation_toggle_.OffContent(box_value(L"关闭"));
    auto_pair_punctuation_toggle_.IsOn(settings_.auto_pair_punctuation);
    auto_pair_punctuation_toggle_.MinWidth(120.0);
    AppendSettingsRow(input_rows, L"自动补全成对符号",
                      L"输入左引号、左括号或左书名号时自动补全右侧符号",
                      auto_pair_punctuation_toggle_);
    Border input_card = CreateCard();
    input_card.Child(input_rows);
    content.Children().Append(input_card);

    content.Children().Append(CreateSectionTitle(L"候选翻页"));
    StackPanel paging_rows;
    int page_key_index = 0;
    if (settings_.page_key_set == ziliu::core::PageKeySet::kSemicolonApostrophe) {
      page_key_index = 1;
    } else if (settings_.page_key_set == ziliu::core::PageKeySet::kBrackets) {
      page_key_index = 2;
    }
    page_key_combo_ = CreateCombo({L"， / 。", L"； / ‘", L"【 / 】"}, page_key_index);
    AppendSettingsRow(paging_rows, L"上一页 / 下一页", L"选择候选窗口的成对翻页按键",
                      page_key_combo_);
    Border paging_card = CreateCard();
    paging_card.Child(paging_rows);
    content.Children().Append(paging_card);

    save_status_ = InfoBar();
    save_status_.IsOpen(false);
    save_status_.IsClosable(true);
    save_status_.Margin(Thickness{0.0, 20.0, 0.0, 0.0});
    content.Children().Append(save_status_);

    Button save_button;
    save_button.Content(box_value(L"保存设置"));
    save_button.HorizontalAlignment(HorizontalAlignment::Left);
    save_button.Margin(Thickness{0.0, 16.0, 0.0, 0.0});
    save_button.Padding(Thickness{24.0, 8.0, 24.0, 8.0});
    ApplyAccentStyle(save_button);
    save_button.Click([this](IInspectable const&, RoutedEventArgs const&) { SaveFromControls(); });
    content.Children().Append(save_button);

    ScrollViewer scroll;
    scroll.Content(content);
    root.Children().Append(scroll);
    window_.Content(root);
  }

  void BuildQuickMenu() {
    settings_ = LoadSettings();

    Grid root;
    if (const auto background = ThemeResource<Brush>(L"LayerFillColorDefaultBrush")) {
      root.Background(background);
    }

    StackPanel content;
    content.Padding(Thickness{18.0});
    content.Spacing(14.0);

    Grid heading;
    ColumnDefinition icon_column;
    icon_column.Width(GridLength{1.0, GridUnitType::Auto});
    heading.ColumnDefinitions().Append(icon_column);
    ColumnDefinition text_column;
    text_column.Width(GridLength{1.0, GridUnitType::Star});
    heading.ColumnDefinitions().Append(text_column);

    Border emblem;
    emblem.Width(38.0);
    emblem.Height(38.0);
    emblem.CornerRadius(CornerRadius{9.0});
    if (const auto accent = ThemeResource<Brush>(L"AccentFillColorDefaultBrush")) {
      emblem.Background(accent);
    }
    FontIcon emblem_icon;
    emblem_icon.Glyph(L"\uE765");
    emblem_icon.FontSize(19.0);
    if (const auto foreground = ThemeResource<Brush>(L"TextOnAccentFillColorPrimaryBrush")) {
      emblem_icon.Foreground(foreground);
    }
    emblem.Child(emblem_icon);
    heading.Children().Append(emblem);

    StackPanel heading_text;
    heading_text.Margin(Thickness{12.0, 0.0, 0.0, 0.0});
    heading_text.VerticalAlignment(VerticalAlignment::Center);
    Grid::SetColumn(heading_text, 1);

    TextBlock title;
    title.Text(L"字流 Ziliu");
    title.FontSize(18.0);
    title.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
    heading_text.Children().Append(title);
    TextBlock hint;
    hint.Text(L"左键切换中英文 · 右键打开菜单");
    hint.FontSize(11.0);
    hint.Opacity(0.62);
    heading_text.Children().Append(hint);
    heading.Children().Append(heading_text);
    content.Children().Append(heading);

    character_set_toggle_ = ToggleSwitch();
    character_set_toggle_.OffContent(box_value(L"简体"));
    character_set_toggle_.OnContent(box_value(L"繁体"));
    character_set_toggle_.IsOn(settings_.character_set ==
                               ziliu::core::CharacterSet::kTraditional);
    character_set_toggle_.MinWidth(120.0);
    character_set_toggle_.Toggled([this](IInspectable const&, RoutedEventArgs const&) {
      settings_.character_set = character_set_toggle_.IsOn()
                                    ? ziliu::core::CharacterSet::kTraditional
                                    : ziliu::core::CharacterSet::kSimplified;
      static_cast<void>(SaveSettings(settings_));
    });
    StackPanel character_rows;
    AppendSettingsRow(character_rows, L"简繁转换", L"切换候选词的简体或繁体输出",
                      character_set_toggle_);
    Border character_card = CreateCard();
    character_card.Child(character_rows);
    content.Children().Append(character_card);

    Button open_settings;
    StackPanel open_settings_content;
    open_settings_content.Orientation(Orientation::Horizontal);
    open_settings_content.Spacing(8.0);
    FontIcon settings_icon;
    settings_icon.Glyph(L"\uE713");
    settings_icon.FontSize(16.0);
    open_settings_content.Children().Append(settings_icon);
    TextBlock settings_text;
    settings_text.Text(L"打开完整设置");
    open_settings_content.Children().Append(settings_text);
    open_settings.Content(open_settings_content);
    open_settings.HorizontalAlignment(HorizontalAlignment::Stretch);
    open_settings.HorizontalContentAlignment(HorizontalAlignment::Center);
    open_settings.Padding(Thickness{14.0, 8.0, 14.0, 8.0});
    ApplyAccentStyle(open_settings);
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

    Border surface = CreateCard();
    surface.Margin(Thickness{8.0});
    surface.Child(content);
    root.Children().Append(surface);
    window_.Content(root);
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
  Application::Start([options, &app](auto&&) {
    app = make_self<ZiliuSettingsApp>(options);
  });
  return 0;
}
