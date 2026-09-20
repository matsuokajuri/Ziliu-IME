#include "pch.h"

#include "MainWindow.xaml.h"
#include "sogou_theme_import.h"

#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"

#include "ziliu/core/settings.h"

#include <dwmapi.h>
#include <dwrite.h>
#include <microsoft.ui.xaml.window.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cctype>
#include <cstddef>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
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

std::optional<std::filesystem::path> ThemesDirectoryPath() {
  const auto settings_path = SettingsFilePath();
  if (!settings_path.has_value()) {
    return std::nullopt;
  }
  return settings_path->parent_path() / L"Themes";
}

std::wstring QuoteCommandLineArgument(std::wstring_view value) {
  std::wstring quoted = L"\"";
  std::size_t backslash_count = 0;
  for (const wchar_t character : value) {
    if (character == L'\\') {
      ++backslash_count;
      continue;
    }
    if (character == L'"') {
      quoted.append(backslash_count * 2 + 1, L'\\');
      quoted.push_back(L'"');
      backslash_count = 0;
      continue;
    }
    quoted.append(backslash_count, L'\\');
    backslash_count = 0;
    quoted.push_back(character);
  }
  quoted.append(backslash_count * 2, L'\\');
  quoted.push_back(L'"');
  return quoted;
}

bool RunArchiveTool(const std::filesystem::path& archive_tool,
                    const std::vector<std::wstring>& arguments,
                    const std::optional<std::filesystem::path>& output_path) {
  std::wstring command_line = QuoteCommandLineArgument(archive_tool.native());
  for (const auto& argument : arguments) {
    command_line.push_back(L' ');
    command_line += QuoteCommandLineArgument(argument);
  }

  HANDLE output_handle = INVALID_HANDLE_VALUE;
  HANDLE input_handle = INVALID_HANDLE_VALUE;
  if (output_path.has_value()) {
    SECURITY_ATTRIBUTES security_attributes{
        sizeof(security_attributes),
        nullptr,
        TRUE,
    };
    output_handle =
        CreateFileW(output_path->c_str(), GENERIC_WRITE, FILE_SHARE_READ, &security_attributes,
                    CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
    if (output_handle == INVALID_HANDLE_VALUE) {
      return false;
    }
    input_handle = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               &security_attributes, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                               nullptr);
    if (input_handle == INVALID_HANDLE_VALUE) {
      CloseHandle(output_handle);
      return false;
    }
  }

  STARTUPINFOW startup_info{sizeof(startup_info)};
  startup_info.dwFlags = STARTF_USESHOWWINDOW;
  startup_info.wShowWindow = SW_HIDE;
  if (output_handle != INVALID_HANDLE_VALUE) {
    startup_info.dwFlags |= STARTF_USESTDHANDLES;
    startup_info.hStdOutput = output_handle;
    startup_info.hStdError = output_handle;
    startup_info.hStdInput = input_handle;
  }
  PROCESS_INFORMATION process_info{};
  const BOOL created =
      CreateProcessW(archive_tool.c_str(), command_line.data(), nullptr, nullptr,
                     output_handle != INVALID_HANDLE_VALUE, CREATE_NO_WINDOW, nullptr, nullptr,
                     &startup_info, &process_info);
  if (output_handle != INVALID_HANDLE_VALUE) {
    CloseHandle(output_handle);
    CloseHandle(input_handle);
  }
  if (created == FALSE) {
    return false;
  }

  const DWORD wait_result = WaitForSingleObject(process_info.hProcess, 15000);
  if (wait_result == WAIT_TIMEOUT) {
    static_cast<void>(TerminateProcess(process_info.hProcess, ERROR_TIMEOUT));
    static_cast<void>(WaitForSingleObject(process_info.hProcess, 1000));
  }
  DWORD exit_code = std::numeric_limits<DWORD>::max();
  static_cast<void>(GetExitCodeProcess(process_info.hProcess, &exit_code));
  CloseHandle(process_info.hThread);
  CloseHandle(process_info.hProcess);
  return wait_result == WAIT_OBJECT_0 && exit_code == 0;
}

bool IsSafeArchiveEntry(std::string_view entry) {
  while (!entry.empty() && (entry.back() == '\r' || entry.back() == '\n')) {
    entry.remove_suffix(1);
  }
  if (entry.empty()) {
    return false;
  }
  if (entry.back() == '/') {
    entry.remove_suffix(1);
  }
  return !entry.empty() && ziliu::core::IsSafeThemeAssetPath(entry);
}

struct ArchiveEntry {
  std::string utf8_path;
  std::wstring wide_path;
  bool directory = false;
  std::uint16_t flags = 0;
  std::uint16_t compression_method = 0;
  std::uint32_t crc32 = 0;
  std::uint32_t compressed_size = 0;
  std::uint32_t uncompressed_size = 0;
  std::uint32_t local_header_offset = 0;
};

struct ExpectedArchiveEntry {
  std::wstring path;
  bool directory = false;
};

struct ArchiveValidationResult {
  std::vector<ArchiveEntry> archive_entries;
  std::vector<ExpectedArchiveEntry> extracted_entries;
  std::wstring error;

  [[nodiscard]] bool ok() const noexcept { return error.empty(); }
};

template <typename T>
std::optional<T> ReadLittleEndian(const std::vector<std::uint8_t>& bytes,
                                  std::size_t offset) {
  static_assert(std::is_unsigned_v<T>);
  if (offset > bytes.size() || bytes.size() - offset < sizeof(T)) {
    return std::nullopt;
  }
  std::uint64_t value = 0;
  for (std::size_t index = 0; index < sizeof(T); ++index) {
    value |= static_cast<std::uint64_t>(bytes[offset + index]) << (index * 8U);
  }
  return static_cast<T>(value);
}

std::optional<std::wstring> Utf8ToWide(std::string_view value) {
  if (value.empty() ||
      value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
    return std::nullopt;
  }
  const int required = MultiByteToWideChar(
      CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
  if (required <= 0) {
    return std::nullopt;
  }
  std::wstring wide(static_cast<std::size_t>(required), L'\0');
  if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                          static_cast<int>(value.size()), wide.data(), required) != required) {
    return std::nullopt;
  }
  return wide;
}

bool OrdinalEqualsIgnoreCase(std::wstring_view left, std::wstring_view right) {
  if (left.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
      right.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
    return false;
  }
  return CompareStringOrdinal(left.data(), static_cast<int>(left.size()), right.data(),
                              static_cast<int>(right.size()), TRUE) == CSTR_EQUAL;
}

bool IsReservedWindowsPathComponent(std::wstring_view component) {
  const std::size_t extension = component.find(L'.');
  const std::wstring_view stem = component.substr(0, extension);
  static constexpr std::array<std::wstring_view, 4> reserved_names{
      L"CON",
      L"PRN",
      L"AUX",
      L"NUL",
  };
  for (const auto reserved : reserved_names) {
    if (OrdinalEqualsIgnoreCase(stem, reserved)) {
      return true;
    }
  }
  if (stem.size() == 4 &&
      (OrdinalEqualsIgnoreCase(stem.substr(0, 3), L"COM") ||
       OrdinalEqualsIgnoreCase(stem.substr(0, 3), L"LPT")) &&
      stem[3] >= L'1' && stem[3] <= L'9') {
    return true;
  }
  return false;
}

bool IsSafeWindowsArchivePath(std::wstring_view path) {
  std::size_t begin = 0;
  while (begin < path.size()) {
    const std::size_t end = path.find(L'/', begin);
    const std::wstring_view component =
        path.substr(begin, end == std::wstring_view::npos ? path.size() - begin : end - begin);
    if (component.empty() || component.back() == L'.' || component.back() == L' ' ||
        component.find_first_of(L"<>\"|?*") != std::wstring_view::npos ||
        IsReservedWindowsPathComponent(component)) {
      return false;
    }
    if (end == std::wstring_view::npos) {
      break;
    }
    begin = end + 1;
  }
  return true;
}

bool HasDisallowedOrMalformedZipExtra(const std::vector<std::uint8_t>& bytes,
                                      std::size_t offset, std::size_t length) {
  const std::size_t end = offset + length;
  while (offset < end) {
    if (end - offset < 4) {
      return true;
    }
    const auto identifier = ReadLittleEndian<std::uint16_t>(bytes, offset);
    const auto field_length = ReadLittleEndian<std::uint16_t>(bytes, offset + 2);
    if (!identifier.has_value() || !field_length.has_value() ||
        static_cast<std::size_t>(*field_length) > end - offset - 4) {
      return true;
    }
    if (*identifier == 0x0001 || *identifier == 0x9901 || *identifier == 0x7075) {
      return true;
    }
    offset += 4 + static_cast<std::size_t>(*field_length);
  }
  return false;
}

ArchiveValidationResult ValidateZltArchive(const std::filesystem::path& archive_path) {
  const auto fail = [](std::wstring message) {
    ArchiveValidationResult result;
    result.error = std::move(message);
    return result;
  };

  constexpr std::size_t kEndOfCentralDirectorySize = 22;
  constexpr std::size_t kMaximumZipCommentBytes = 65535;
  constexpr std::size_t kMaximumArchiveBytes =
      ziliu::core::kMaximumThemePackageBytes * 2;
  constexpr std::uint32_t kEndOfCentralDirectorySignature = 0x06054B50;
  constexpr std::uint32_t kCentralDirectorySignature = 0x02014B50;
  constexpr std::uint32_t kLocalFileHeaderSignature = 0x04034B50;
  constexpr std::uint32_t kDataDescriptorSignature = 0x08074B50;

  std::ifstream stream(archive_path, std::ios::binary | std::ios::ate);
  if (!stream) {
    return fail(L"无法读取所选主题包。");
  }
  const std::streampos end_position = stream.tellg();
  if (end_position < static_cast<std::streampos>(kEndOfCentralDirectorySize) ||
      end_position > static_cast<std::streampos>(kMaximumArchiveBytes)) {
    return fail(L"主题包不是受支持的标准 ZIP，或文件过大。");
  }
  const auto archive_size = static_cast<std::size_t>(end_position);
  std::vector<std::uint8_t> bytes(archive_size);
  stream.seekg(0, std::ios::beg);
  stream.read(reinterpret_cast<char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
  if (!stream || stream.gcount() != static_cast<std::streamsize>(bytes.size())) {
    return fail(L"无法完整读取所选主题包。");
  }

  const std::size_t first_eocd_offset =
      bytes.size() > kEndOfCentralDirectorySize + kMaximumZipCommentBytes
          ? bytes.size() - kEndOfCentralDirectorySize - kMaximumZipCommentBytes
          : 0;
  std::optional<std::size_t> eocd_offset;
  for (std::size_t offset = bytes.size() - kEndOfCentralDirectorySize;; --offset) {
    const auto signature = ReadLittleEndian<std::uint32_t>(bytes, offset);
    const auto comment_length = ReadLittleEndian<std::uint16_t>(bytes, offset + 20);
    if (signature == kEndOfCentralDirectorySignature && comment_length.has_value() &&
        offset + kEndOfCentralDirectorySize + *comment_length == bytes.size()) {
      eocd_offset = offset;
      break;
    }
    if (offset == first_eocd_offset) {
      break;
    }
  }
  if (!eocd_offset.has_value()) {
    return fail(L"主题包缺少标准 ZIP 中央目录。");
  }

  const auto disk_number = ReadLittleEndian<std::uint16_t>(bytes, *eocd_offset + 4);
  const auto central_disk = ReadLittleEndian<std::uint16_t>(bytes, *eocd_offset + 6);
  const auto entries_on_disk = ReadLittleEndian<std::uint16_t>(bytes, *eocd_offset + 8);
  const auto entry_count = ReadLittleEndian<std::uint16_t>(bytes, *eocd_offset + 10);
  const auto central_size = ReadLittleEndian<std::uint32_t>(bytes, *eocd_offset + 12);
  const auto central_offset = ReadLittleEndian<std::uint32_t>(bytes, *eocd_offset + 16);
  if (!disk_number.has_value() || !central_disk.has_value() ||
      !entries_on_disk.has_value() || !entry_count.has_value() ||
      !central_size.has_value() || !central_offset.has_value() ||
      *disk_number != 0 || *central_disk != 0 || *entries_on_disk != *entry_count ||
      *entry_count == 0 || *entry_count == std::numeric_limits<std::uint16_t>::max() ||
      *central_size == std::numeric_limits<std::uint32_t>::max() ||
      *central_offset == std::numeric_limits<std::uint32_t>::max() ||
      *entry_count > ziliu::core::kMaximumThemePackageEntries ||
      *central_offset > *eocd_offset || *central_size != *eocd_offset - *central_offset) {
    return fail(L"主题包使用了不支持的分卷、ZIP64 或异常中央目录。");
  }

  ArchiveValidationResult result;
  result.archive_entries.reserve(*entry_count);
  std::size_t cursor = *central_offset;
  std::uintmax_t total_bytes = 0;
  bool has_manifest = false;
  for (std::size_t index = 0; index < *entry_count; ++index) {
    if (cursor > *eocd_offset || *eocd_offset - cursor < 46 ||
        ReadLittleEndian<std::uint32_t>(bytes, cursor) != kCentralDirectorySignature) {
      return fail(L"主题包中央目录条目损坏。");
    }
    const auto version_made_by = ReadLittleEndian<std::uint16_t>(bytes, cursor + 4);
    const auto version_needed = ReadLittleEndian<std::uint16_t>(bytes, cursor + 6);
    const auto flags = ReadLittleEndian<std::uint16_t>(bytes, cursor + 8);
    const auto method = ReadLittleEndian<std::uint16_t>(bytes, cursor + 10);
    const auto crc32 = ReadLittleEndian<std::uint32_t>(bytes, cursor + 16);
    const auto compressed_size = ReadLittleEndian<std::uint32_t>(bytes, cursor + 20);
    const auto uncompressed_size = ReadLittleEndian<std::uint32_t>(bytes, cursor + 24);
    const auto name_length = ReadLittleEndian<std::uint16_t>(bytes, cursor + 28);
    const auto extra_length = ReadLittleEndian<std::uint16_t>(bytes, cursor + 30);
    const auto comment_length = ReadLittleEndian<std::uint16_t>(bytes, cursor + 32);
    const auto disk_start = ReadLittleEndian<std::uint16_t>(bytes, cursor + 34);
    const auto external_attributes = ReadLittleEndian<std::uint32_t>(bytes, cursor + 38);
    const auto local_header_offset = ReadLittleEndian<std::uint32_t>(bytes, cursor + 42);
    if (!version_made_by.has_value() || !version_needed.has_value() || !flags.has_value() ||
        !method.has_value() || !crc32.has_value() || !compressed_size.has_value() ||
        !uncompressed_size.has_value() || !name_length.has_value() ||
        !extra_length.has_value() || !comment_length.has_value() ||
        !disk_start.has_value() || !external_attributes.has_value() ||
        !local_header_offset.has_value()) {
      return fail(L"主题包中央目录字段不完整。");
    }
    const std::size_t variable_size = static_cast<std::size_t>(*name_length) +
                                      static_cast<std::size_t>(*extra_length) +
                                      static_cast<std::size_t>(*comment_length);
    if (*name_length == 0 || variable_size > *eocd_offset - cursor - 46) {
      return fail(L"主题包中央目录名称或扩展字段损坏。");
    }
    if (*version_needed > 20 || *disk_start != 0 ||
        *compressed_size == std::numeric_limits<std::uint32_t>::max() ||
        *uncompressed_size == std::numeric_limits<std::uint32_t>::max() ||
        *local_header_offset == std::numeric_limits<std::uint32_t>::max()) {
      return fail(L"主题包使用了 ZIP64 或不支持的 ZIP 功能。");
    }
    constexpr std::uint16_t kAllowedGeneralPurposeFlags = 0x080E;
    if ((*flags & ~kAllowedGeneralPurposeFlags) != 0 ||
        (*method != 0 && *method != 8) || (*method == 0 && (*flags & 0x0006) != 0)) {
      return fail(L"主题包只能使用未加密的 Store 或 Deflate 压缩。");
    }

    const std::size_t name_offset = cursor + 46;
    const std::string entry_name(
        reinterpret_cast<const char*>(bytes.data() + name_offset), *name_length);
    const bool has_non_ascii =
        std::any_of(entry_name.begin(), entry_name.end(),
                    [](unsigned char character) { return character >= 0x80; });
    if (entry_name.find('\0') != std::string::npos ||
        (has_non_ascii && (*flags & 0x0800) == 0) || !IsSafeArchiveEntry(entry_name)) {
      return fail(L"主题包包含无效编码或不安全路径。");
    }
    const bool is_directory = entry_name.back() == '/';
    const std::string normalized_name =
        is_directory ? entry_name.substr(0, entry_name.size() - 1) : entry_name;
    const auto wide_name = Utf8ToWide(normalized_name);
    if (!wide_name.has_value() || !IsSafeWindowsArchivePath(*wide_name)) {
      return fail(L"主题包包含 Windows 无法安全创建的路径。");
    }
    for (const auto& previous : result.archive_entries) {
      if (OrdinalEqualsIgnoreCase(previous.wide_path, *wide_name)) {
        return fail(L"主题包包含重复或仅大小写不同的路径。");
      }
    }

    const std::uint32_t unix_mode = *external_attributes >> 16U;
    const std::uint32_t unix_type = unix_mode & 0170000U;
    if (unix_type != 0 &&
        unix_type != (is_directory ? 0040000U : 0100000U)) {
      return fail(L"主题包不能包含符号链接、硬链接或特殊文件。");
    }
    const bool dos_directory = (*external_attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    if ((dos_directory && !is_directory) ||
        (is_directory &&
         (*uncompressed_size != 0 ||
          (*method == 0 && *compressed_size != 0)))) {
      return fail(L"主题包目录条目的类型或大小无效。");
    }
    if (*uncompressed_size > ziliu::core::kMaximumThemeAssetBytes ||
        total_bytes > ziliu::core::kMaximumThemePackageBytes - *uncompressed_size) {
      return fail(L"主题包超过单文件 8 MiB 或总计 32 MiB 的安全限制。");
    }
    if (*method == 0 && *compressed_size != *uncompressed_size) {
      return fail(L"主题包中的 Store 条目大小无效。");
    }
    total_bytes += *uncompressed_size;

    const std::size_t extra_offset = name_offset + *name_length;
    if (HasDisallowedOrMalformedZipExtra(bytes, extra_offset, *extra_length)) {
      return fail(L"主题包使用了 ZIP64、AES 或名称重映射扩展。");
    }
    if (normalized_name == ziliu::core::kThemeManifestFileName) {
      if (is_directory || has_manifest) {
        return fail(L"主题包必须只包含一个根目录 manifest.json。");
      }
      has_manifest = true;
    }

    result.archive_entries.push_back(
        {normalized_name, *wide_name, is_directory, *flags, *method, *crc32,
         *compressed_size, *uncompressed_size, *local_header_offset});
    cursor += 46 + variable_size;
  }
  if (cursor != *eocd_offset || !has_manifest) {
    return fail(L"主题包中央目录不完整，或缺少根目录 manifest.json。");
  }

  std::vector<std::pair<std::size_t, std::size_t>> local_ranges;
  local_ranges.reserve(result.archive_entries.size());
  for (const auto& entry : result.archive_entries) {
    const std::size_t local_offset = entry.local_header_offset;
    if (local_offset > *central_offset || *central_offset - local_offset < 30 ||
        ReadLittleEndian<std::uint32_t>(bytes, local_offset) !=
            kLocalFileHeaderSignature) {
      return fail(L"主题包本地文件头损坏。");
    }
    const auto local_flags = ReadLittleEndian<std::uint16_t>(bytes, local_offset + 6);
    const auto local_method = ReadLittleEndian<std::uint16_t>(bytes, local_offset + 8);
    const auto local_crc32 = ReadLittleEndian<std::uint32_t>(bytes, local_offset + 14);
    const auto local_compressed_size =
        ReadLittleEndian<std::uint32_t>(bytes, local_offset + 18);
    const auto local_uncompressed_size =
        ReadLittleEndian<std::uint32_t>(bytes, local_offset + 22);
    const auto local_name_length =
        ReadLittleEndian<std::uint16_t>(bytes, local_offset + 26);
    const auto local_extra_length =
        ReadLittleEndian<std::uint16_t>(bytes, local_offset + 28);
    if (!local_flags.has_value() || !local_method.has_value() ||
        !local_crc32.has_value() || !local_compressed_size.has_value() ||
        !local_uncompressed_size.has_value() || !local_name_length.has_value() ||
        !local_extra_length.has_value() || *local_flags != entry.flags ||
        *local_method != entry.compression_method) {
      return fail(L"主题包本地文件头与中央目录不一致。");
    }
    const std::size_t local_variable_size =
        static_cast<std::size_t>(*local_name_length) + *local_extra_length;
    if (local_variable_size > *central_offset - local_offset - 30) {
      return fail(L"主题包本地文件头长度无效。");
    }
    const std::size_t local_name_offset = local_offset + 30;
    const std::string local_name(
        reinterpret_cast<const char*>(bytes.data() + local_name_offset),
        *local_name_length);
    const std::string expected_local_name =
        entry.directory ? entry.utf8_path + "/" : entry.utf8_path;
    if (local_name != expected_local_name ||
        HasDisallowedOrMalformedZipExtra(
            bytes, local_name_offset + *local_name_length, *local_extra_length)) {
      return fail(L"主题包本地文件名或扩展字段与中央目录不一致。");
    }
    const bool uses_data_descriptor = (entry.flags & 0x0008) != 0;
    if ((!uses_data_descriptor &&
         (*local_crc32 != entry.crc32 ||
          *local_compressed_size != entry.compressed_size ||
          *local_uncompressed_size != entry.uncompressed_size)) ||
        (uses_data_descriptor &&
         ((*local_crc32 != 0 && *local_crc32 != entry.crc32) ||
          (*local_compressed_size != 0 &&
           *local_compressed_size != entry.compressed_size) ||
          (*local_uncompressed_size != 0 &&
           *local_uncompressed_size != entry.uncompressed_size)))) {
      return fail(L"主题包本地文件大小或校验值与中央目录不一致。");
    }
    const std::size_t data_offset = local_name_offset + local_variable_size;
    if (entry.compressed_size > *central_offset - data_offset) {
      return fail(L"主题包压缩数据越过中央目录。");
    }
    std::size_t local_end = data_offset + entry.compressed_size;
    if (uses_data_descriptor) {
      if (ReadLittleEndian<std::uint32_t>(bytes, local_end) ==
          kDataDescriptorSignature) {
        local_end += 4;
      }
      if (local_end > *central_offset || *central_offset - local_end < 12) {
        return fail(L"主题包数据描述符不完整。");
      }
      const auto descriptor_crc32 = ReadLittleEndian<std::uint32_t>(bytes, local_end);
      const auto descriptor_compressed_size =
          ReadLittleEndian<std::uint32_t>(bytes, local_end + 4);
      const auto descriptor_uncompressed_size =
          ReadLittleEndian<std::uint32_t>(bytes, local_end + 8);
      if (descriptor_crc32 != entry.crc32 ||
          descriptor_compressed_size != entry.compressed_size ||
          descriptor_uncompressed_size != entry.uncompressed_size) {
        return fail(L"主题包数据描述符与中央目录不一致。");
      }
      local_end += 12;
    }
    local_ranges.emplace_back(local_offset, local_end);
  }
  std::sort(local_ranges.begin(), local_ranges.end());
  if (local_ranges.empty() || local_ranges.front().first != 0 ||
      local_ranges.back().second != *central_offset) {
    return fail(L"主题包包含 ZIP 文件结构之外的附加数据。");
  }
  for (std::size_t index = 1; index < local_ranges.size(); ++index) {
    if (local_ranges[index - 1].second != local_ranges[index].first) {
      return fail(L"主题包本地文件数据重叠或存在未声明区段。");
    }
  }

  const auto add_expected_entry = [&result](std::wstring path, bool directory) {
    for (const auto& existing : result.extracted_entries) {
      if (!OrdinalEqualsIgnoreCase(existing.path, path)) {
        continue;
      }
      return existing.path == path && existing.directory == directory;
    }
    result.extracted_entries.push_back({std::move(path), directory});
    return true;
  };
  for (const auto& entry : result.archive_entries) {
    if (!add_expected_entry(entry.wide_path, entry.directory)) {
      return fail(L"主题包路径的文件与目录关系冲突。");
    }
  }
  for (const auto& entry : result.archive_entries) {
    std::size_t separator = entry.wide_path.find(L'/');
    while (separator != std::wstring::npos) {
      if (!add_expected_entry(entry.wide_path.substr(0, separator), true)) {
        return fail(L"主题包路径的大小写或文件与目录关系冲突。");
      }
      separator = entry.wide_path.find(L'/', separator + 1);
    }
  }
  return result;
}

std::optional<std::filesystem::path> CreateThemeStagingDirectory(
    const std::filesystem::path& themes_directory) {
  GUID identifier{};
  if (FAILED(CoCreateGuid(&identifier))) {
    return std::nullopt;
  }
  wchar_t identifier_text[40]{};
  if (StringFromGUID2(identifier, identifier_text,
                      static_cast<int>(std::size(identifier_text))) <= 0) {
    return std::nullopt;
  }
  std::wstring directory_name = L".staging-";
  directory_name += identifier_text;
  std::replace(directory_name.begin(), directory_name.end(), L'{', L'_');
  std::replace(directory_name.begin(), directory_name.end(), L'}', L'_');
  const std::filesystem::path staging_directory = themes_directory / directory_name;
  std::error_code error;
  if (!std::filesystem::create_directories(staging_directory, error) || error) {
    return std::nullopt;
  }
  return staging_directory;
}

std::optional<std::uintmax_t> ReadPngPixelCount(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  std::array<unsigned char, 24> header{};
  stream.read(reinterpret_cast<char*>(header.data()),
              static_cast<std::streamsize>(header.size()));
  static constexpr std::array<unsigned char, 8> signature{
      0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
  if (stream.gcount() != static_cast<std::streamsize>(header.size()) ||
      !std::equal(signature.begin(), signature.end(), header.begin()) ||
      header[12] != 'I' || header[13] != 'H' || header[14] != 'D' ||
      header[15] != 'R') {
    return std::nullopt;
  }
  const auto read_big_endian = [&header](std::size_t offset) {
    return (static_cast<std::uint32_t>(header[offset]) << 24U) |
           (static_cast<std::uint32_t>(header[offset + 1]) << 16U) |
           (static_cast<std::uint32_t>(header[offset + 2]) << 8U) |
           static_cast<std::uint32_t>(header[offset + 3]);
  };
  const std::uint32_t width = read_big_endian(16);
  const std::uint32_t height = read_big_endian(20);
  if (width == 0 || height == 0 || width > ziliu::core::kMaximumThemeImageDimension ||
      height > ziliu::core::kMaximumThemeImageDimension) {
    return std::nullopt;
  }
  return static_cast<std::uintmax_t>(width) * height;
}

struct FileIdentity {
  DWORD volume_serial_number = 0;
  DWORD file_index_high = 0;
  DWORD file_index_low = 0;
};

bool SameFileIdentity(const FileIdentity& left, const FileIdentity& right) noexcept {
  return left.volume_serial_number == right.volume_serial_number &&
         left.file_index_high == right.file_index_high &&
         left.file_index_low == right.file_index_low;
}

std::optional<FileIdentity> ReadRegularFileIdentity(
    const std::filesystem::path& path) {
  const HANDLE handle =
      CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                  OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    return std::nullopt;
  }
  BY_HANDLE_FILE_INFORMATION information{};
  const BOOL read = GetFileInformationByHandle(handle, &information);
  CloseHandle(handle);
  if (read == FALSE ||
      (information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 ||
      (information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
      information.nNumberOfLinks != 1) {
    return std::nullopt;
  }
  return FileIdentity{
      information.dwVolumeSerialNumber,
      information.nFileIndexHigh,
      information.nFileIndexLow,
  };
}

bool ValidateExtractedThemeDirectory(
    const std::filesystem::path& directory,
    const std::vector<ExpectedArchiveEntry>& expected_entries,
    std::string* manifest_contents) {
  std::size_t entry_count = 0;
  std::uintmax_t total_bytes = 0;
  std::uintmax_t total_decoded_pixels = 0;
  std::vector<bool> seen_entries(expected_entries.size(), false);
  std::vector<FileIdentity> file_identities;
  file_identities.reserve(expected_entries.size());
  std::error_code iterator_error;
  const std::filesystem::recursive_directory_iterator end;
  for (std::filesystem::recursive_directory_iterator iterator(
           directory, std::filesystem::directory_options::skip_permission_denied,
           iterator_error);
       !iterator_error && iterator != end; iterator.increment(iterator_error)) {
    ++entry_count;
    if (entry_count > expected_entries.size()) {
      return false;
    }
    const auto status = iterator->symlink_status(iterator_error);
    if (iterator_error || std::filesystem::is_symlink(status) ||
        std::filesystem::is_other(status)) {
      return false;
    }
    const auto relative =
        std::filesystem::relative(iterator->path(), directory, iterator_error);
    if (iterator_error) {
      return false;
    }
    const std::wstring relative_path = relative.generic_wstring();
    const auto expected = std::find_if(
        expected_entries.begin(), expected_entries.end(),
        [&relative_path](const ExpectedArchiveEntry& entry) {
          return entry.path == relative_path;
        });
    if (expected == expected_entries.end()) {
      return false;
    }
    const std::size_t expected_index =
        static_cast<std::size_t>(std::distance(expected_entries.begin(), expected));
    const bool is_directory = std::filesystem::is_directory(status);
    if (seen_entries[expected_index] || expected->directory != is_directory) {
      return false;
    }
    seen_entries[expected_index] = true;
    if (std::filesystem::is_directory(status)) {
      continue;
    }
    if (!std::filesystem::is_regular_file(status)) {
      return false;
    }
    const auto file_identity = ReadRegularFileIdentity(iterator->path());
    if (!file_identity.has_value() ||
        std::any_of(file_identities.begin(), file_identities.end(),
                    [&file_identity](const FileIdentity& previous) {
                      return SameFileIdentity(previous, *file_identity);
                    })) {
      return false;
    }
    file_identities.push_back(*file_identity);
    const std::uintmax_t size = iterator->file_size(iterator_error);
    if (iterator_error || size > ziliu::core::kMaximumThemeAssetBytes ||
        total_bytes > ziliu::core::kMaximumThemePackageBytes - size) {
      return false;
    }
    total_bytes += size;

    const std::wstring extension = iterator->path().extension().wstring();
    const bool is_manifest =
        relative.generic_string() == std::string(ziliu::core::kThemeManifestFileName);
    if (!is_manifest && _wcsicmp(extension.c_str(), L".png") != 0 &&
        _wcsicmp(extension.c_str(), L".apng") != 0) {
      return false;
    }
    if (!is_manifest) {
      const auto pixel_count = ReadPngPixelCount(iterator->path());
      if (!pixel_count.has_value() ||
          total_decoded_pixels > ziliu::core::kMaximumThemeDecodedPixels - *pixel_count) {
        return false;
      }
      total_decoded_pixels += *pixel_count;
    }
  }
  if (iterator_error) {
    return false;
  }
  if (!std::all_of(seen_entries.begin(), seen_entries.end(),
                   [](bool seen) { return seen; })) {
    return false;
  }

  const std::filesystem::path manifest_path =
      directory / std::string(ziliu::core::kThemeManifestFileName);
  std::ifstream manifest_stream(manifest_path, std::ios::binary);
  if (!manifest_stream) {
    return false;
  }
  *manifest_contents = {std::istreambuf_iterator<char>(manifest_stream),
                        std::istreambuf_iterator<char>()};
  return manifest_contents->size() <= ziliu::core::kMaximumThemeManifestBytes;
}

bool ThemeAssetsExist(const ziliu::core::ThemeManifest& manifest,
                      const std::filesystem::path& directory) {
  std::vector<std::string_view> assets;
  const auto append_asset = [&assets](const std::string& asset) {
    if (!asset.empty()) {
      assets.push_back(asset);
    }
  };
  const auto append_button = [&append_asset](
                                 const std::optional<ziliu::core::ThemeButtonImages>& button) {
    if (!button.has_value()) {
      return;
    }
    append_asset(button->normal);
    append_asset(button->hover);
    append_asset(button->pressed);
  };
  const auto append_surface = [&append_asset, &append_button](
                                  const ziliu::core::ThemeSurface& surface) {
    if (surface.background.has_value()) {
      append_asset(surface.background->asset);
    }
    if (surface.separator.has_value()) {
      append_asset(surface.separator->asset);
    }
    append_button(surface.previous_button);
    append_button(surface.next_button);
    append_button(surface.expand_button);
    append_button(surface.collapse_button);
    append_button(surface.menu_button);
  };
  const auto append_appearance = [&append_surface](
                                     const ziliu::core::ThemeAppearance& appearance) {
    append_surface(appearance.horizontal);
    append_surface(appearance.vertical);
  };
  append_asset(manifest.preview_asset);
  append_appearance(manifest.light);
  if (manifest.dark.has_value()) {
    append_appearance(*manifest.dark);
  }

  for (const auto asset : assets) {
    const auto wide_asset = winrt::to_hstring(asset);
    const std::filesystem::path asset_path = directory / wide_asset.c_str();
    std::error_code status_error;
    const auto status = std::filesystem::symlink_status(asset_path, status_error);
    if (status_error || !std::filesystem::is_regular_file(status) ||
        std::filesystem::is_symlink(status)) {
      return false;
    }
  }
  return true;
}

struct StagingDirectoryCleanup {
  explicit StagingDirectoryCleanup(std::filesystem::path value)
      : directory(std::move(value)) {}

  std::filesystem::path directory;

  StagingDirectoryCleanup(const StagingDirectoryCleanup&) = delete;
  StagingDirectoryCleanup& operator=(const StagingDirectoryCleanup&) = delete;

  ~StagingDirectoryCleanup() {
    if (!directory.empty()) {
      std::error_code cleanup_error;
      std::filesystem::remove_all(directory, cleanup_error);
    }
  }

  void Release() noexcept { directory.clear(); }
};

struct ThemeImportResult {
  std::optional<ziliu::core::ThemeManifest> manifest;
  std::wstring error;

  [[nodiscard]] bool ok() const noexcept {
    return manifest.has_value() && error.empty();
  }
};

ThemeImportResult InstallThemePackage(const std::filesystem::path& source_archive,
                                      const std::filesystem::path& themes_directory) {
  if (_wcsicmp(source_archive.extension().c_str(), L".ssf") == 0) {
    auto imported = ziliu::settings::InstallSogouSsfPackage(source_archive, themes_directory);
    return {std::move(imported.manifest), std::move(imported.error)};
  }
  const auto fail = [](std::wstring message) {
    ThemeImportResult result;
    result.error = std::move(message);
    return result;
  };

  std::error_code directory_error;
  std::filesystem::create_directories(themes_directory, directory_error);
  if (directory_error) {
    return fail(L"无法创建字流主题目录。");
  }
  const auto staging_directory = CreateThemeStagingDirectory(themes_directory);
  if (!staging_directory.has_value()) {
    return fail(L"无法创建临时导入目录。");
  }
  StagingDirectoryCleanup cleanup{*staging_directory};

  const std::filesystem::path stable_archive = *staging_directory / L"package.zlt";
  std::error_code copy_error;
  std::filesystem::copy_file(source_archive, stable_archive,
                             std::filesystem::copy_options::none, copy_error);
  if (copy_error) {
    return fail(L"无法把主题包复制到安全的临时目录。");
  }

  const ArchiveValidationResult archive_validation = ValidateZltArchive(stable_archive);
  if (!archive_validation.ok()) {
    return fail(archive_validation.error);
  }

  wchar_t system_directory[MAX_PATH]{};
  const UINT system_directory_length =
      GetSystemDirectoryW(system_directory, static_cast<UINT>(std::size(system_directory)));
  if (system_directory_length == 0 ||
      system_directory_length >= static_cast<UINT>(std::size(system_directory))) {
    return fail(L"无法定位系统归档工具。");
  }
  const std::filesystem::path archive_tool =
      std::filesystem::path(system_directory) / L"tar.exe";
  const std::filesystem::path extraction_log = *staging_directory / L"extract.log";
  const bool extracted =
      RunArchiveTool(archive_tool,
                     {L"-xf", stable_archive.native(), L"-C", staging_directory->native()},
                     extraction_log);
  std::error_code remove_temporary_error;
  std::filesystem::remove(stable_archive, remove_temporary_error);
  remove_temporary_error.clear();
  std::filesystem::remove(extraction_log, remove_temporary_error);
  if (!extracted) {
    return fail(L"主题包解压失败或数据校验不通过。");
  }

  std::string manifest_contents;
  if (!ValidateExtractedThemeDirectory(*staging_directory,
                                       archive_validation.extracted_entries,
                                       &manifest_contents)) {
    return fail(L"主题解压结果与 ZIP 目录不一致，包含不支持的文件，或超过安全限制。");
  }
  const auto parsed = ziliu::core::ParseThemeManifest(manifest_contents);
  if (!parsed.ok()) {
    std::wstring detail = L"manifest.json 无效。";
    if (!parsed.issues.empty()) {
      const auto issue =
          Utf8ToWide(parsed.issues.front().path + " " + parsed.issues.front().message);
      if (issue.has_value()) {
        detail = std::wstring(L"manifest.json 无效：") + *issue;
      }
    }
    return fail(std::move(detail));
  }
  if (ziliu::core::IsReservedThemeId(parsed.manifest.id)) {
    return fail(L"主题不能使用字流内置主题的保留标识。");
  }
  if (!ThemeAssetsExist(parsed.manifest, *staging_directory)) {
    return fail(L"manifest.json 引用的主题资源不存在。");
  }

  const std::filesystem::path target_directory =
      themes_directory / std::filesystem::path(parsed.manifest.id);
  std::error_code target_error;
  if (std::filesystem::exists(target_directory, target_error) || target_error) {
    return fail(L"同一标识的主题已经安装，请先删除现有主题。");
  }
  std::filesystem::rename(*staging_directory, target_directory, target_error);
  if (target_error) {
    return fail(L"无法把主题安装到本地主题目录。");
  }

  cleanup.Release();
  ThemeImportResult result;
  result.manifest = parsed.manifest;
  return result;
}

class ScopedHandle final {
 public:
  ScopedHandle() = default;
  explicit ScopedHandle(HANDLE value) noexcept : value_(value) {}
  ~ScopedHandle() { Reset(); }

  ScopedHandle(const ScopedHandle&) = delete;
  ScopedHandle& operator=(const ScopedHandle&) = delete;

  ScopedHandle(ScopedHandle&& other) noexcept
      : value_(std::exchange(other.value_, INVALID_HANDLE_VALUE)) {}
  ScopedHandle& operator=(ScopedHandle&& other) noexcept {
    if (this != &other) {
      Reset(std::exchange(other.value_, INVALID_HANDLE_VALUE));
    }
    return *this;
  }

  [[nodiscard]] HANDLE Get() const noexcept { return value_; }
  [[nodiscard]] explicit operator bool() const noexcept {
    return value_ != nullptr && value_ != INVALID_HANDLE_VALUE;
  }
  void Reset(HANDLE value = INVALID_HANDLE_VALUE) noexcept {
    if (value_ != nullptr && value_ != INVALID_HANDLE_VALUE) {
      CloseHandle(value_);
    }
    value_ = value;
  }

 private:
  HANDLE value_ = INVALID_HANDLE_VALUE;
};

std::optional<FileIdentity> ReadDirectoryIdentity(HANDLE handle) {
  if (handle == nullptr || handle == INVALID_HANDLE_VALUE) {
    return std::nullopt;
  }
  BY_HANDLE_FILE_INFORMATION information{};
  const BOOL read = GetFileInformationByHandle(handle, &information);
  if (read == FALSE ||
      (information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
      (information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
    return std::nullopt;
  }
  return FileIdentity{
      information.dwVolumeSerialNumber,
      information.nFileIndexHigh,
      information.nFileIndexLow,
  };
}

std::optional<FileIdentity> ReadDirectoryIdentity(
    const std::filesystem::path& directory) {
  ScopedHandle handle(CreateFileW(
      directory.c_str(), FILE_READ_ATTRIBUTES,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
      OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
      nullptr));
  return ReadDirectoryIdentity(handle.Get());
}

ScopedHandle OpenThemeDirectoryForRename(
    const std::filesystem::path& directory) {
  ScopedHandle handle(CreateFileW(
      directory.c_str(), FILE_READ_ATTRIBUTES | DELETE,
      FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
      OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
      nullptr));
  if (!ReadDirectoryIdentity(handle.Get()).has_value()) {
    handle.Reset();
  }
  return handle;
}

bool RenameDirectoryHandle(HANDLE directory_handle,
                           const std::filesystem::path& destination,
                           DWORD* rename_error = nullptr) {
  const std::wstring destination_name = destination.native();
  if (directory_handle == nullptr || directory_handle == INVALID_HANDLE_VALUE ||
      destination_name.empty() ||
      destination_name.size() >
          (std::numeric_limits<DWORD>::max() / sizeof(wchar_t))) {
    if (rename_error != nullptr) {
      *rename_error = ERROR_INVALID_PARAMETER;
    }
    return false;
  }
  const std::size_t information_size =
      offsetof(FILE_RENAME_INFO, FileName) +
      (destination_name.size() + 1) * sizeof(wchar_t);
  if (information_size > std::numeric_limits<DWORD>::max()) {
    if (rename_error != nullptr) {
      *rename_error = ERROR_INVALID_PARAMETER;
    }
    return false;
  }
  std::vector<std::uint8_t> storage(information_size);
  auto* information = reinterpret_cast<FILE_RENAME_INFO*>(storage.data());
  information->ReplaceIfExists = FALSE;
  information->RootDirectory = nullptr;
  information->FileNameLength =
      static_cast<DWORD>(destination_name.size() * sizeof(wchar_t));
  std::copy(destination_name.begin(), destination_name.end(),
            information->FileName);
  information->FileName[destination_name.size()] = L'\0';
  const BOOL renamed = SetFileInformationByHandle(
      directory_handle, FileRenameInfo, information,
      static_cast<DWORD>(storage.size()));
  const DWORD error = renamed != FALSE ? ERROR_SUCCESS : GetLastError();
  if (rename_error != nullptr) {
    *rename_error = error;
  }
  return renamed != FALSE;
}

std::optional<std::filesystem::path> QuarantineThemeDirectory(
    HANDLE directory_handle, const std::filesystem::path& themes_directory,
    DWORD* rename_error) {
  if (rename_error != nullptr) {
    *rename_error = ERROR_SUCCESS;
  }
  GUID identifier{};
  if (FAILED(CoCreateGuid(&identifier))) {
    return std::nullopt;
  }
  wchar_t identifier_text[40]{};
  if (StringFromGUID2(identifier, identifier_text,
                      static_cast<int>(std::size(identifier_text))) <= 0) {
    return std::nullopt;
  }
  std::wstring directory_name = L".deleting-";
  directory_name += identifier_text;
  std::replace(directory_name.begin(), directory_name.end(), L'{', L'_');
  std::replace(directory_name.begin(), directory_name.end(), L'}', L'_');
  const std::filesystem::path quarantine =
      themes_directory / directory_name;
  if (!RenameDirectoryHandle(directory_handle, quarantine, rename_error)) {
    return std::nullopt;
  }
  return quarantine;
}

bool ValidateThemeTreeForDeletion(const std::filesystem::path& directory) {
  std::error_code status_error;
  const auto root_status = std::filesystem::symlink_status(directory, status_error);
  if (status_error || !std::filesystem::is_directory(root_status) ||
      std::filesystem::is_symlink(root_status)) {
    return false;
  }

  std::size_t entry_count = 0;
  std::vector<FileIdentity> file_identities;
  std::error_code iterator_error;
  const std::filesystem::recursive_directory_iterator end;
  for (std::filesystem::recursive_directory_iterator iterator(
           directory, std::filesystem::directory_options::skip_permission_denied,
           iterator_error);
       !iterator_error && iterator != end; iterator.increment(iterator_error)) {
    if (++entry_count > ziliu::core::kMaximumThemePackageEntries) {
      return false;
    }
    const auto status = iterator->symlink_status(iterator_error);
    if (iterator_error || std::filesystem::is_symlink(status) ||
        std::filesystem::is_other(status)) {
      return false;
    }
    if (std::filesystem::is_directory(status)) {
      continue;
    }
    if (!std::filesystem::is_regular_file(status)) {
      return false;
    }
    const auto identity = ReadRegularFileIdentity(iterator->path());
    if (!identity.has_value() ||
        std::any_of(file_identities.begin(), file_identities.end(),
                    [&identity](const FileIdentity& previous) {
                      return SameFileIdentity(previous, *identity);
                    })) {
      return false;
    }
    file_identities.push_back(*identity);
  }
  return !iterator_error;
}

bool RemoveQuarantinedThemeTree(const std::filesystem::path& directory,
                                HANDLE directory_handle) {
  if (!ValidateThemeTreeForDeletion(directory) ||
      directory_handle == nullptr ||
      directory_handle == INVALID_HANDLE_VALUE) {
    return false;
  }
  std::error_code iterator_error;
  std::filesystem::directory_iterator iterator(
      directory, std::filesystem::directory_options::skip_permission_denied,
      iterator_error);
  const std::filesystem::directory_iterator end;
  while (!iterator_error && iterator != end) {
    const std::filesystem::path child = iterator->path();
    iterator.increment(iterator_error);
    if (iterator_error) {
      return false;
    }
    std::error_code remove_error;
    static_cast<void>(std::filesystem::remove_all(child, remove_error));
    if (remove_error) {
      return false;
    }
  }
  if (iterator_error) {
    return false;
  }
  FILE_DISPOSITION_INFO disposition{TRUE};
  return SetFileInformationByHandle(directory_handle, FileDispositionInfo,
                                    &disposition,
                                    sizeof(disposition)) != FALSE;
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

Microsoft::UI::Xaml::Media::Brush SystemBrush(
    std::wstring_view resource_key, std::uint32_t fallback_color) {
  const auto key = winrt::box_value(winrt::hstring(resource_key));
  const auto resources = Microsoft::UI::Xaml::Application::Current().Resources();
  if (resources.HasKey(key)) {
    const auto value = resources.Lookup(key);
    if (const auto brush = value.try_as<Microsoft::UI::Xaml::Media::Brush>()) {
      return brush;
    }
  }
  return Microsoft::UI::Xaml::Media::SolidColorBrush(ToColor(fallback_color));
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
  // Both full Settings and the quick menu must apply the saved theme before
  // either surface is shown. Quick-menu launches skip InitializeSettingsControls.
  ThemeCombo().SelectedIndex(static_cast<int>(settings_.theme_mode));
  ApplyThemeFromControls();

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
          candidate_preview_.Hide();
          static_cast<void>(SaveFromControls());
        });
    Closed([this](auto const&, auto const&) {
      candidate_preview_closed_ = true;
      candidate_preview_.Hide();
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
  ReloadThemeCatalog();
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
  ImportThemeButton().Click([this](auto const&, auto const&) { ImportTheme(); });
  CandidatePreviewHost().Loaded([this](auto const&, auto const&) {
    EnsureCandidatePreview();
    UpdateCandidatePreview();
  });
  CandidatePreviewHost().SizeChanged(
      [this](auto const&, auto const&) { UpdateCandidatePreview(); });
  CandidatePreviewHost().Unloaded(
      [this](auto const&, auto const&) { candidate_preview_.Hide(); });
  AppearancePage().ViewChanged(
      [this](auto const&, auto const&) { UpdateCandidatePreview(); });
  RootGrid().SizeChanged(
      [this](auto const&, auto const&) { UpdateCandidatePreview(); });
  const auto save_appearance = [this]() {
    if (SaveFromControls()) {
      AppearanceInfoBar().IsOpen(false);
      return true;
    }
    AppearanceInfoBar().Severity(
        Microsoft::UI::Xaml::Controls::InfoBarSeverity::Error);
    AppearanceInfoBar().Title(L"无法保存外观设置");
    AppearanceInfoBar().Message(
        L"设置文件无法写入；本次更改可能在重新启动后恢复。");
    AppearanceInfoBar().IsOpen(true);
    return false;
  };
  ThemeCombo().SelectionChanged([this, save_appearance](auto const&, auto const&) {
    if (suppress_appearance_events_) {
      return;
    }
    ApplyThemeFromControls();
    settings_.theme_mode =
        static_cast<ziliu::core::ThemeMode>(ThemeCombo().SelectedIndex());
    RebuildThemeList();
    UpdateCandidatePreview();
    static_cast<void>(save_appearance());
  });
  const auto update_appearance_state =
      [this, save_appearance](auto const&, auto const&) {
    if (suppress_appearance_events_) {
      return;
    }
    UpdateAppearanceControlStates();
    UpdateCandidatePreview();
    static_cast<void>(save_appearance());
  };
  CustomColorsToggle().Toggled(update_appearance_state);
  CustomFontsToggle().Toggled(update_appearance_state);
  CustomFontSizeToggle().Toggled(update_appearance_state);
  const auto update_preview = [this, save_appearance](auto const&, auto const&) {
    if (suppress_appearance_events_) {
      return;
    }
    UpdateCandidatePreview();
    static_cast<void>(save_appearance());
  };
  LayoutCombo().SelectionChanged(update_preview);
  CandidateCountCombo().SelectionChanged(update_preview);
  CandidatePageModeCombo().SelectionChanged(update_preview);
  CandidateChineseFontCombo().SelectionChanged(update_preview);
  CandidateEnglishFontCombo().SelectionChanged(update_preview);
  CandidateFontSizeCombo().SelectionChanged(update_preview);
  CandidateScaleToggle().Toggled(update_preview);
  const auto update_color = [this, save_appearance](auto const&, auto const&) {
    if (suppress_appearance_events_) {
      return;
    }
    UpdateColorSwatches();
    UpdateCandidatePreview();
    static_cast<void>(save_appearance());
  };
  PreeditColorPicker().ColorChanged(update_color);
  HighlightedColorPicker().ColorChanged(update_color);
  CandidateTextColorPicker().ColorChanged(update_color);
  BackgroundColorPicker().ColorChanged(update_color);
  ResetAppearanceButton().Click([this, save_appearance](auto const&, auto const&) {
    const ziliu::core::Settings previous_settings = settings_;
    suppress_appearance_events_ = true;
    ThemeCombo().SelectedIndex(0);
    settings_.active_theme_id = std::string(ziliu::core::kDefaultThemeId);
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
    suppress_appearance_events_ = false;
    UpdateAppearanceControlStates();
    UpdateColorSwatches();
    RebuildThemeList();
    UpdateCandidatePreview();
    if (save_appearance()) {
      return;
    }

    settings_ = previous_settings;
    suppress_appearance_events_ = true;
    ThemeCombo().SelectedIndex(static_cast<int>(settings_.theme_mode));
    LayoutCombo().SelectedIndex(
        settings_.candidate_layout == ziliu::core::CandidateLayout::kHorizontal
            ? 0
            : 1);
    CandidateCountCombo().SelectedIndex(static_cast<int>(
        std::clamp(settings_.candidate_count,
                   ziliu::core::kMinimumCandidateCount,
                   ziliu::core::kMaximumCandidateCount) -
        ziliu::core::kMinimumCandidateCount));
    CandidatePageModeCombo().SelectedIndex(
        settings_.candidate_page_mode ==
                ziliu::core::CandidatePageMode::kMultiLine
            ? 1
            : 0);
    CustomColorsToggle().IsOn(settings_.custom_candidate_colors);
    PreeditColorPicker().Color(ToColor(settings_.preedit_color));
    HighlightedColorPicker().Color(
        ToColor(settings_.highlighted_candidate_color));
    CandidateTextColorPicker().Color(ToColor(settings_.candidate_text_color));
    BackgroundColorPicker().Color(
        ToColor(settings_.candidate_background_color));
    CustomFontsToggle().IsOn(settings_.custom_candidate_fonts);
    SelectFontComboBox(CandidateChineseFontCombo(),
                       settings_.candidate_chinese_font_family,
                       L"Microsoft YaHei UI");
    SelectFontComboBox(CandidateEnglishFontCombo(),
                       settings_.candidate_english_font_family,
                       L"Segoe UI Variable Text");
    CustomFontSizeToggle().IsOn(settings_.custom_candidate_font_size);
    CandidateFontSizeCombo().SelectedIndex(static_cast<int>(
        std::clamp(settings_.candidate_font_size,
                   ziliu::core::kMinimumCandidateFontSize,
                   ziliu::core::kMaximumCandidateFontSize) -
        ziliu::core::kMinimumCandidateFontSize));
    CandidateScaleToggle().IsOn(settings_.candidate_scale_with_text);
    suppress_appearance_events_ = false;
    ApplyThemeFromControls();
    UpdateAppearanceControlStates();
    UpdateColorSwatches();
    RebuildThemeList();
    UpdateCandidatePreview();
    AppearanceInfoBar().Title(L"未能恢复默认外观");
    AppearanceInfoBar().Message(
        L"设置文件无法写入，界面已恢复到更改前的外观。");
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
    UpdateCandidatePreview();
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
    RebuildThemeList();
  } else {
    CommonPage().Visibility(visible);
  }
  if (page != L"appearance") {
    candidate_preview_.Hide();
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
  const bool candidate_style_enabled =
      settings_.active_theme_id == ziliu::core::kDefaultThemeId;
  CandidateWindowStyleCard().Opacity(candidate_style_enabled ? 1.0 : 0.45);
  CandidateWindowStyleCustomThemeHint().Visibility(
      candidate_style_enabled ? Microsoft::UI::Xaml::Visibility::Collapsed
                              : Microsoft::UI::Xaml::Visibility::Visible);

  const bool colors_enabled = candidate_style_enabled && CustomColorsToggle().IsOn();
  CandidateColorControls().IsHitTestVisible(colors_enabled);
  CandidateColorControls().Opacity(CustomColorsToggle().IsOn() ? 1.0 : 0.45);
  CustomColorsToggle().IsEnabled(candidate_style_enabled);
  PreeditColorButton().IsEnabled(colors_enabled);
  HighlightedColorButton().IsEnabled(colors_enabled);
  CandidateTextColorButton().IsEnabled(colors_enabled);
  BackgroundColorButton().IsEnabled(colors_enabled);
  CustomFontsToggle().IsEnabled(candidate_style_enabled);
  CandidateChineseFontCombo().IsEnabled(candidate_style_enabled && CustomFontsToggle().IsOn());
  CandidateEnglishFontCombo().IsEnabled(candidate_style_enabled && CustomFontsToggle().IsOn());
  CustomFontSizeToggle().IsEnabled(candidate_style_enabled);
  CandidateFontSizeCombo().IsEnabled(candidate_style_enabled && CustomFontSizeToggle().IsOn());
  CandidateScaleToggle().IsEnabled(candidate_style_enabled);
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

void MainWindow::EnsureCandidatePreview() {
  if (candidate_preview_ready_) {
    return;
  }
  HWND window_handle = nullptr;
  Microsoft::UI::Xaml::Window window = *this;
  if (FAILED(window.as<::IWindowNative>()->get_WindowHandle(&window_handle)) ||
      window_handle == nullptr) {
    return;
  }
  candidate_preview_ready_ = candidate_preview_.CreatePreview(window_handle);
}

std::optional<RECT> MainWindow::CandidatePreviewBounds(RECT& viewport_bounds) {
  if (!CandidatePreviewHost().IsLoaded() || CandidatePreviewHost().ActualWidth() <= 0.0 ||
      CandidatePreviewHost().ActualHeight() <= 0.0 || RootGrid().XamlRoot() == nullptr) {
    return std::nullopt;
  }
  const auto transform = CandidatePreviewHost().TransformToVisual(RootGrid());
  const auto origin = transform.TransformPoint({0.0F, 0.0F});
  const double scale = RootGrid().XamlRoot().RasterizationScale();
  const auto to_pixel = [scale](double value) {
    return static_cast<LONG>(std::lround(value * scale));
  };
  RECT bounds{};
  bounds.left = to_pixel(origin.X);
  bounds.top = to_pixel(origin.Y);
  bounds.right = bounds.left + to_pixel(CandidatePreviewHost().ActualWidth());
  bounds.bottom = bounds.top + to_pixel(CandidatePreviewHost().ActualHeight());
  const auto viewport_transform = AppearancePage().TransformToVisual(RootGrid());
  const auto viewport_origin = viewport_transform.TransformPoint({0.0F, 0.0F});
  viewport_bounds = {to_pixel(viewport_origin.X), to_pixel(viewport_origin.Y),
                     to_pixel(viewport_origin.X + AppearancePage().ActualWidth()),
                     to_pixel(viewport_origin.Y + AppearancePage().ActualHeight())};
  return bounds;
}

void MainWindow::ReloadThemeCatalog() {
  installed_themes_.clear();
  installed_themes_.push_back({ziliu::core::MakeDefaultThemeManifest(), {}});
  if (const auto themes_directory = ThemesDirectoryPath(); themes_directory.has_value()) {
    std::error_code directory_error;
    std::filesystem::create_directories(*themes_directory, directory_error);
    if (!directory_error) {
      auto installed = ziliu::core::LoadInstalledThemes(*themes_directory);
      installed_themes_.insert(installed_themes_.end(),
                               std::make_move_iterator(installed.begin()),
                               std::make_move_iterator(installed.end()));
    }
  }

  const auto active = std::find_if(
      installed_themes_.begin(), installed_themes_.end(), [this](const auto& theme) {
        return theme.manifest.id == settings_.active_theme_id;
      });
  if (active == installed_themes_.end()) {
    settings_.active_theme_id = ziliu::core::MakeDefaultThemeManifest().id;
    if (!SaveSettings(settings_)) {
      ThemeInfoBar().Severity(Microsoft::UI::Xaml::Controls::InfoBarSeverity::Error);
      ThemeInfoBar().Title(L"无法保存主题设置");
      ThemeInfoBar().Message(L"当前主题已回退为字流默认，但设置文件无法写入。");
      ThemeInfoBar().IsOpen(true);
    }
  }
  RebuildThemeList();
}

void MainWindow::RebuildThemeList() {
  ThemeList().Children().Clear();
  bool first_theme = true;
  for (const auto& installed : installed_themes_) {
    const auto& manifest = installed.manifest;
    const bool selected = manifest.id == settings_.active_theme_id;
    const bool can_delete = !installed.directory.empty();

    if (!first_theme) {
      Microsoft::UI::Xaml::Controls::Border divider;
      divider.Height(1.0);
      divider.Margin(Microsoft::UI::Xaml::Thickness{20.0, 0.0, 20.0, 0.0});
      divider.Background(SystemBrush(L"DividerStrokeColorDefaultBrush", 0xE5E7EB));
      ThemeList().Children().Append(divider);
    }
    first_theme = false;

    Microsoft::UI::Xaml::Controls::Border row;
    row.Padding(Microsoft::UI::Xaml::Thickness{20.0, 13.0, 16.0, 13.0});

    Microsoft::UI::Xaml::Controls::Grid layout;
    layout.ColumnSpacing(14.0);
    Microsoft::UI::Xaml::Controls::ColumnDefinition icon_column;
    icon_column.Width(Microsoft::UI::Xaml::GridLengthHelper::Auto());
    layout.ColumnDefinitions().Append(icon_column);
    layout.ColumnDefinitions().Append(Microsoft::UI::Xaml::Controls::ColumnDefinition());
    Microsoft::UI::Xaml::Controls::ColumnDefinition status_column;
    status_column.Width(Microsoft::UI::Xaml::GridLengthHelper::Auto());
    layout.ColumnDefinitions().Append(status_column);
    Microsoft::UI::Xaml::Controls::ColumnDefinition delete_column;
    delete_column.Width(Microsoft::UI::Xaml::GridLengthHelper::Auto());
    layout.ColumnDefinitions().Append(delete_column);

    Microsoft::UI::Xaml::Controls::FontIcon theme_icon;
    theme_icon.Glyph(L"\uE790");
    theme_icon.FontSize(20.0);
    theme_icon.Foreground(SystemBrush(L"TextFillColorSecondaryBrush", 0x5F6368));
    theme_icon.VerticalAlignment(Microsoft::UI::Xaml::VerticalAlignment::Center);
    layout.Children().Append(theme_icon);

    Microsoft::UI::Xaml::Controls::StackPanel labels;
    labels.Spacing(2.0);
    labels.VerticalAlignment(Microsoft::UI::Xaml::VerticalAlignment::Center);
    Microsoft::UI::Xaml::Controls::TextBlock name;
    name.Text(winrt::to_hstring(manifest.name));
    name.FontSize(15.0);
    name.TextTrimming(Microsoft::UI::Xaml::TextTrimming::CharacterEllipsis);
    Microsoft::UI::Xaml::Controls::TextBlock metadata;
    std::wstring metadata_text = winrt::to_hstring(manifest.author).c_str();
    metadata_text += L" · 版本 ";
    metadata_text += winrt::to_hstring(manifest.version).c_str();
    metadata_text += can_delete ? L" · 本地主题" : L" · 内置主题";
    metadata.Text(winrt::hstring(metadata_text));
    metadata.FontSize(12.0);
    metadata.Foreground(SystemBrush(L"TextFillColorSecondaryBrush", 0x5F6368));
    metadata.TextTrimming(Microsoft::UI::Xaml::TextTrimming::CharacterEllipsis);
    labels.Children().Append(name);
    labels.Children().Append(metadata);
    Microsoft::UI::Xaml::Controls::Grid::SetColumn(labels, 1);
    layout.Children().Append(labels);

    if (selected) {
      Microsoft::UI::Xaml::Controls::StackPanel status;
      status.Orientation(Microsoft::UI::Xaml::Controls::Orientation::Horizontal);
      status.Spacing(5.0);
      status.VerticalAlignment(Microsoft::UI::Xaml::VerticalAlignment::Center);
      Microsoft::UI::Xaml::Controls::FontIcon check;
      check.Glyph(L"\uE73E");
      check.FontSize(13.0);
      check.Foreground(SystemBrush(L"AccentTextFillColorPrimaryBrush", 0x0067C0));
      Microsoft::UI::Xaml::Controls::TextBlock status_text;
      status_text.Text(L"正在使用");
      status_text.FontSize(13.0);
      status_text.Foreground(SystemBrush(L"AccentTextFillColorPrimaryBrush", 0x0067C0));
      status.Children().Append(check);
      status.Children().Append(status_text);
      Microsoft::UI::Xaml::Controls::Grid::SetColumn(status, 2);
      layout.Children().Append(status);
    } else {
      Microsoft::UI::Xaml::Controls::Button apply_button;
      apply_button.Content(winrt::box_value(L"应用"));
      apply_button.MinWidth(72.0);
      apply_button.VerticalAlignment(Microsoft::UI::Xaml::VerticalAlignment::Center);
      Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(
          apply_button,
          winrt::hstring(std::wstring(L"应用主题 ") + winrt::to_hstring(manifest.name).c_str()));
      const std::string theme_id = manifest.id;
      apply_button.Click([this, theme_id](auto const&, auto const&) { SelectTheme(theme_id); });
      Microsoft::UI::Xaml::Controls::Grid::SetColumn(apply_button, 2);
      layout.Children().Append(apply_button);
    }

    if (can_delete) {
      Microsoft::UI::Xaml::Controls::Button delete_button;
      delete_button.Content(winrt::box_value(L"删除"));
      delete_button.Margin(Microsoft::UI::Xaml::Thickness{0.0, 0.0, 0.0, 0.0});
      delete_button.VerticalAlignment(Microsoft::UI::Xaml::VerticalAlignment::Center);
      Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(
          delete_button,
          winrt::hstring(std::wstring(L"删除主题 ") + winrt::to_hstring(manifest.name).c_str()));
      const std::string theme_id = manifest.id;
      delete_button.Click(
          [this, theme_id](auto const&, auto const&) { DeleteTheme(theme_id); });
      Microsoft::UI::Xaml::Controls::Grid::SetColumn(delete_button, 3);
      layout.Children().Append(delete_button);
    }

    row.Child(layout);
    ThemeList().Children().Append(row);
  }
  UpdateAppearanceControlStates();
}

bool MainWindow::SelectTheme(std::string_view theme_id) {
  const auto selected =
      std::find_if(installed_themes_.begin(), installed_themes_.end(),
                   [theme_id](const auto& installed) {
                     return installed.manifest.id == theme_id;
                   });
  if (selected == installed_themes_.end()) {
    return false;
  }
  if (settings_.active_theme_id == theme_id) {
    return true;
  }
  const std::string previous_theme_id = settings_.active_theme_id;
  settings_.active_theme_id = std::string(theme_id);
  if (!SaveFromControls()) {
    settings_.active_theme_id = previous_theme_id;
    ThemeInfoBar().Severity(Microsoft::UI::Xaml::Controls::InfoBarSeverity::Error);
    ThemeInfoBar().Title(L"无法应用主题");
    ThemeInfoBar().Message(L"设置文件无法写入，当前主题没有改变。");
    ThemeInfoBar().IsOpen(true);
    return false;
  }
  RebuildThemeList();
  return true;
}

void MainWindow::UpdateCandidatePreview() {
  if (candidate_preview_closed_ || candidate_preview_updating_) {
    return;
  }
  if (AppearancePage().Visibility() != Microsoft::UI::Xaml::Visibility::Visible ||
      !CandidatePreviewHost().IsLoaded()) {
    candidate_preview_.Hide();
    return;
  }
  candidate_preview_updating_ = true;
  struct PreviewUpdateScope {
    bool& updating;
    ~PreviewUpdateScope() { updating = false; }
  } update_scope{candidate_preview_updating_};
  CandidatePreviewWindow().Visibility(Microsoft::UI::Xaml::Visibility::Collapsed);
  CandidatePreviewUnavailable().Visibility(Microsoft::UI::Xaml::Visibility::Visible);

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

  EnsureCandidatePreview();
  RECT viewport_bounds{};
  const auto preview_bounds = CandidatePreviewBounds(viewport_bounds);
  if (candidate_preview_ready_ && preview_bounds.has_value()) {
    CandidatePreviewUnavailable().Visibility(Microsoft::UI::Xaml::Visibility::Collapsed);
    ziliu::core::CompositionSnapshot snapshot;
    snapshot.preedit = L"ziliu'shu'ru'fa";
    snapshot.candidates = {
        {L"字流", L"", 1.0}, {L"输入法", L"", 0.9}, {L"简洁", L"", 0.8},
        {L"高效", L"", 0.7}, {L"纯粹", L"", 0.6},  {L"中文", L"", 0.5},
        {L"拼音", L"", 0.4}, {L"开源", L"", 0.3},  {L"轻巧", L"", 0.2},
    };
    snapshot.highlighted_index = 0;
    candidate_preview_.ShowPreview(snapshot, *preview_bounds, preview_settings, 0, &viewport_bounds);
    return;
  }

  candidate_preview_.Hide();
  CandidatePreviewUnavailable().Visibility(Microsoft::UI::Xaml::Visibility::Collapsed);
  CandidatePreviewWindow().Visibility(Microsoft::UI::Xaml::Visibility::Visible);

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

winrt::fire_and_forget MainWindow::ImportTheme() {
  const auto show_error = [this](std::wstring_view message) {
    ThemeInfoBar().Severity(Microsoft::UI::Xaml::Controls::InfoBarSeverity::Error);
    ThemeInfoBar().Title(L"无法导入主题");
    ThemeInfoBar().Message(winrt::hstring(message));
    ThemeInfoBar().IsOpen(true);
  };

  std::filesystem::path archive_path;
  {
    ::Microsoft::WRL::ComPtr<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(dialog.GetAddressOf())))) {
      show_error(L"无法打开文件选择器。");
      co_return;
    }
    static constexpr COMDLG_FILTERSPEC filters[] = {
        {L"字流 / 搜狗主题包 (*.zlt;*.ssf)", L"*.zlt;*.ssf"},
        {L"所有文件 (*.*)", L"*.*"},
    };
    static_cast<void>(
        dialog->SetFileTypes(static_cast<UINT>(std::size(filters)), filters));
    static_cast<void>(dialog->SetDefaultExtension(L"zlt"));
    static_cast<void>(dialog->SetTitle(L"导入字流主题"));

    HWND window_handle = nullptr;
    Microsoft::UI::Xaml::Window window = *this;
    if (FAILED(window.as<::IWindowNative>()->get_WindowHandle(&window_handle))) {
      show_error(L"无法取得设置窗口句柄。");
      co_return;
    }
    const HRESULT show_result = dialog->Show(window_handle);
    if (show_result == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
      co_return;
    }
    if (FAILED(show_result)) {
      show_error(L"文件选择器打开失败。");
      co_return;
    }

    ::Microsoft::WRL::ComPtr<IShellItem> selected_item;
    if (FAILED(dialog->GetResult(selected_item.GetAddressOf()))) {
      show_error(L"无法读取所选文件。");
      co_return;
    }
    wchar_t* selected_path_text = nullptr;
    if (FAILED(selected_item->GetDisplayName(SIGDN_FILESYSPATH,
                                             &selected_path_text)) ||
        selected_path_text == nullptr) {
      show_error(L"所选项目不是本地文件。");
      co_return;
    }
    archive_path = std::filesystem::path(selected_path_text);
    CoTaskMemFree(selected_path_text);
  }
  if (_wcsicmp(archive_path.extension().c_str(), L".zlt") != 0 &&
      _wcsicmp(archive_path.extension().c_str(), L".ssf") != 0) {
    show_error(L"请选择扩展名为 .zlt 或 .ssf 的主题包。");
    co_return;
  }

  const auto themes_directory = ThemesDirectoryPath();
  if (!themes_directory.has_value()) {
    show_error(L"无法定位字流主题目录。");
    co_return;
  }

  ImportThemeButton().IsEnabled(false);
  ThemeInfoBar().Severity(Microsoft::UI::Xaml::Controls::InfoBarSeverity::Informational);
  ThemeInfoBar().Title(L"正在导入主题");
  ThemeInfoBar().Message(winrt::hstring(archive_path.filename().wstring()));
  ThemeInfoBar().IsOpen(true);

  const auto dispatcher = RootGrid().DispatcherQueue();
  const auto weak_this = get_weak();
  co_await winrt::resume_background();

  ThemeImportResult import_result;
  try {
    import_result = InstallThemePackage(archive_path, *themes_directory);
  } catch (...) {
    import_result.error = L"导入过程中发生意外错误，临时文件已清理。";
  }

  const bool queued = dispatcher.TryEnqueue(
      [weak_this, import_result = std::move(import_result)]() mutable {
        const auto strong_this = weak_this.get();
        if (!strong_this) {
          return;
        }
        strong_this->ImportThemeButton().IsEnabled(true);
        if (!import_result.ok()) {
          strong_this->ThemeInfoBar().Severity(
              Microsoft::UI::Xaml::Controls::InfoBarSeverity::Error);
          strong_this->ThemeInfoBar().Title(L"无法导入主题");
          strong_this->ThemeInfoBar().Message(winrt::hstring(import_result.error));
          strong_this->ThemeInfoBar().IsOpen(true);
          return;
        }

        strong_this->ReloadThemeCatalog();
        if (!strong_this->SelectTheme(import_result.manifest->id)) {
          strong_this->ThemeInfoBar().Severity(
              Microsoft::UI::Xaml::Controls::InfoBarSeverity::Error);
          strong_this->ThemeInfoBar().Title(L"主题已安装，但未能应用");
          strong_this->ThemeInfoBar().Message(
              L"无法保存当前主题设置。主题文件已保留，可稍后重新应用。");
          strong_this->ThemeInfoBar().IsOpen(true);
          return;
        }
        strong_this->ThemeInfoBar().Severity(
            Microsoft::UI::Xaml::Controls::InfoBarSeverity::Success);
        strong_this->ThemeInfoBar().Title(L"主题已导入并应用");
        strong_this->ThemeInfoBar().Message(
            winrt::to_hstring(import_result.manifest->name));
        strong_this->ThemeInfoBar().IsOpen(true);
      });
  static_cast<void>(queued);
}

winrt::fire_and_forget MainWindow::DeleteTheme(std::string theme_id) {
  const auto show_error = [this](std::wstring_view message) {
    ThemeInfoBar().Severity(Microsoft::UI::Xaml::Controls::InfoBarSeverity::Error);
    ThemeInfoBar().Title(L"无法删除主题");
    ThemeInfoBar().Message(winrt::hstring(message));
    ThemeInfoBar().IsOpen(true);
  };
  const auto selected = std::find_if(
      installed_themes_.begin(), installed_themes_.end(), [&theme_id](const auto& installed) {
        return installed.manifest.id == theme_id;
      });
  if (selected == installed_themes_.end() || selected->directory.empty()) {
    co_return;
  }
  const std::string theme_name = selected->manifest.name;
  const std::filesystem::path theme_directory = selected->directory;
  const auto themes_directory = ThemesDirectoryPath();
  if (!themes_directory.has_value()) {
    show_error(L"无法定位字流主题目录。");
    co_return;
  }
  const std::filesystem::path expected_directory =
      *themes_directory / std::filesystem::path(theme_id);
  if (theme_directory.lexically_normal() != expected_directory.lexically_normal()) {
    show_error(L"主题目录不在预期位置，已取消删除。");
    co_return;
  }

  std::filesystem::path quarantine_directory;
  ScopedHandle quarantine_handle;
  bool deleting_active_theme = false;
  {
    const auto lifetime = get_strong();
    static_cast<void>(lifetime);
    ScopedHandle directory_handle =
        OpenThemeDirectoryForRename(expected_directory);
    const auto original_identity =
        ReadDirectoryIdentity(directory_handle.Get());
    if (!original_identity.has_value()) {
      show_error(L"主题目录不是可安全删除的本地目录。");
      co_return;
    }

    if (theme_dialog_open_) {
      show_error(L"另一个主题确认窗口仍在打开，请先完成该操作。");
      co_return;
    }
    theme_dialog_open_ = true;
    Microsoft::UI::Xaml::Controls::ContentDialogResult dialog_result{};
    try {
      Microsoft::UI::Xaml::Controls::ContentDialog confirmation;
      confirmation.XamlRoot(RootGrid().XamlRoot());
      confirmation.Title(winrt::box_value(L"删除主题"));
      const std::wstring confirmation_text =
          std::wstring(L"确定要删除“") +
          winrt::to_hstring(theme_name).c_str() +
          L"”吗？此操作会移除本地主题文件。";
      confirmation.Content(
          winrt::box_value(winrt::hstring(confirmation_text)));
      confirmation.PrimaryButtonText(L"删除");
      confirmation.CloseButtonText(L"取消");
      confirmation.DefaultButton(
          Microsoft::UI::Xaml::Controls::ContentDialogButton::Close);
      dialog_result = co_await confirmation.ShowAsync();
    } catch (...) {
      theme_dialog_open_ = false;
      show_error(L"删除确认窗口无法打开。");
      co_return;
    }
    theme_dialog_open_ = false;
    if (dialog_result !=
        Microsoft::UI::Xaml::Controls::ContentDialogResult::Primary) {
      co_return;
    }

    const auto current_identity = ReadDirectoryIdentity(expected_directory);
    const auto current_theme =
        ziliu::core::LoadInstalledTheme(*themes_directory, theme_id);
    if (!current_identity.has_value() ||
        !SameFileIdentity(*original_identity, *current_identity) ||
        !current_theme.has_value() ||
        current_theme->directory.lexically_normal() !=
            expected_directory.lexically_normal()) {
      show_error(L"确认期间主题目录已变化，已取消删除。");
      co_return;
    }

    DWORD quarantine_error = ERROR_SUCCESS;
    const auto quarantined = QuarantineThemeDirectory(
        directory_handle.Get(), *themes_directory, &quarantine_error);
    if (!quarantined.has_value()) {
      std::wstring message = L"无法安全隔离主题目录，未删除任何文件。";
      if (quarantine_error != ERROR_SUCCESS) {
        message += L"（Win32 错误 ";
        message += std::to_wstring(quarantine_error);
        message += L"）";
      }
      show_error(message);
      co_return;
    }
    quarantine_directory = *quarantined;
    const auto quarantined_identity =
        ReadDirectoryIdentity(quarantine_directory);
    if (!quarantined_identity.has_value() ||
        !SameFileIdentity(*original_identity, *quarantined_identity)) {
      show_error(L"隔离后的主题目录身份不一致，已停止删除。");
      co_return;
    }

    deleting_active_theme = theme_id == settings_.active_theme_id;
    if (deleting_active_theme) {
      const std::string previous_theme_id = settings_.active_theme_id;
      settings_.active_theme_id = std::string(ziliu::core::kDefaultThemeId);
      if (!SaveFromControls()) {
        settings_.active_theme_id = previous_theme_id;
        const bool restored =
            RenameDirectoryHandle(directory_handle.Get(), expected_directory);
        ReloadThemeCatalog();
        show_error(restored
                       ? L"无法先切换到默认主题，未删除任何文件。"
                       : L"无法保存默认主题；原主题已安全隔离，但未删除。");
        co_return;
      }
    }
    quarantine_handle = std::move(directory_handle);
    ReloadThemeCatalog();
  }

  ThemeInfoBar().Severity(
      Microsoft::UI::Xaml::Controls::InfoBarSeverity::Informational);
  ThemeInfoBar().Title(L"正在删除主题");
  ThemeInfoBar().Message(winrt::to_hstring(theme_name));
  ThemeInfoBar().IsOpen(true);

  const auto dispatcher = RootGrid().DispatcherQueue();
  const auto weak_this = get_weak();
  co_await winrt::resume_background();

  bool removed = false;
  std::error_code existence_error;
  const bool exists =
      std::filesystem::exists(quarantine_directory, existence_error);
  if (!existence_error && !exists) {
    removed = true;
  } else if (!existence_error) {
    removed = RemoveQuarantinedThemeTree(
        quarantine_directory, quarantine_handle.Get());
  }
  quarantine_handle.Reset();
  if (removed) {
    std::error_code verification_error;
    removed = !std::filesystem::exists(quarantine_directory,
                                       verification_error) &&
              !verification_error;
  }

  const bool queued = dispatcher.TryEnqueue(
      [weak_this, removed, deleting_active_theme, theme_name]() {
        const auto strong_this = weak_this.get();
        if (!strong_this) {
          return;
        }
        strong_this->ReloadThemeCatalog();
        if (!removed) {
          strong_this->ThemeInfoBar().Severity(
              Microsoft::UI::Xaml::Controls::InfoBarSeverity::Error);
          strong_this->ThemeInfoBar().Title(L"主题清理未完成");
          strong_this->ThemeInfoBar().Message(
              deleting_active_theme
                  ? L"主题已停用并移出列表，但部分本地文件无法安全删除。"
                  : L"主题已移出列表，但部分本地文件无法安全删除。");
          strong_this->ThemeInfoBar().IsOpen(true);
          return;
        }
        strong_this->ThemeInfoBar().Severity(
            Microsoft::UI::Xaml::Controls::InfoBarSeverity::Success);
        strong_this->ThemeInfoBar().Title(L"主题已删除");
        strong_this->ThemeInfoBar().Message(winrt::to_hstring(theme_name));
        strong_this->ThemeInfoBar().IsOpen(true);
      });
  static_cast<void>(queued);
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

bool MainWindow::SaveFromControls() {
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

  return SaveSettings(settings_);
}

}  // namespace winrt::ZiliuSettings::implementation

#endif
