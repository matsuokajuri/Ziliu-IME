#include "ziliu/tsf/guids.h"

#include <msctf.h>
#include <windows.h>
#include <wrl/client.h>

#include <array>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>

namespace {

using Microsoft::WRL::ComPtr;

constexpr wchar_t kProfileDescription[] = L"字流拼音";
constexpr wchar_t kClsidRoot[] = L"Software\\Classes\\CLSID\\";

std::wstring GuidToString(REFGUID guid) {
  std::array<wchar_t, 40> buffer{};
  const int length = StringFromGUID2(guid, buffer.data(), static_cast<int>(buffer.size()));
  return length > 0 ? std::wstring(buffer.data(), static_cast<std::size_t>(length - 1))
                    : std::wstring{};
}

std::filesystem::path ExecutableDirectory() {
  std::array<wchar_t, 32768> buffer{};
  const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
  return std::filesystem::path(std::wstring(buffer.data(), length)).parent_path();
}

HRESULT RegisterComServer(const std::filesystem::path& dll_path) {
  const std::wstring key_path =
      std::wstring(kClsidRoot) + GuidToString(ziliu::tsf::kTextServiceClsid) +
      L"\\InprocServer32";
  HKEY key = nullptr;
  const LSTATUS create_result =
      RegCreateKeyExW(HKEY_CURRENT_USER, key_path.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE,
                      KEY_SET_VALUE, nullptr, &key, nullptr);
  if (create_result != ERROR_SUCCESS) {
    return HRESULT_FROM_WIN32(create_result);
  }

  const std::wstring path = dll_path.wstring();
  LSTATUS result = RegSetValueExW(key, nullptr, 0, REG_SZ,
                                  reinterpret_cast<const BYTE*>(path.c_str()),
                                  static_cast<DWORD>((path.size() + 1) * sizeof(wchar_t)));
  if (result == ERROR_SUCCESS) {
    constexpr wchar_t threading_model[] = L"Apartment";
    result = RegSetValueExW(key, L"ThreadingModel", 0, REG_SZ,
                            reinterpret_cast<const BYTE*>(threading_model),
                            static_cast<DWORD>(sizeof(threading_model)));
  }
  RegCloseKey(key);
  return HRESULT_FROM_WIN32(result);
}

HRESULT RegisterCategories() {
  ComPtr<ITfCategoryMgr> category_manager;
  HRESULT result = CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(category_manager.ReleaseAndGetAddressOf()));
  if (FAILED(result)) {
    return result;
  }

  constexpr std::array<const GUID*, 4> categories = {
      &GUID_TFCAT_TIP_KEYBOARD,
      &GUID_TFCAT_TIPCAP_UIELEMENTENABLED,
      &GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT,
      &GUID_TFCAT_TIPCAP_SYSTRAYSUPPORT,
  };
  for (const GUID* category : categories) {
    result = category_manager->RegisterCategory(ziliu::tsf::kTextServiceClsid, *category,
                                                ziliu::tsf::kTextServiceClsid);
    if (FAILED(result)) {
      return result;
    }
  }
  return S_OK;
}

HRESULT RegisterProfile(const std::filesystem::path& dll_path) {
  ComPtr<ITfInputProcessorProfileMgr> profile_manager;
  HRESULT result = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr,
                                    CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(profile_manager.ReleaseAndGetAddressOf()));
  if (FAILED(result)) {
    return result;
  }

  const std::wstring icon_path = dll_path.wstring();
  return profile_manager->RegisterProfile(
      ziliu::tsf::kTextServiceClsid, ziliu::tsf::kSimplifiedChineseLanguageId,
      ziliu::tsf::kSimplifiedChineseProfileGuid, kProfileDescription,
      static_cast<ULONG>(std::size(kProfileDescription) - 1), icon_path.c_str(),
      static_cast<ULONG>(icon_path.size()), 0, nullptr, 0, TRUE, 0);
}

HRESULT Install() {
  const std::filesystem::path dll_path = ExecutableDirectory() / L"ZiliuTIP.dll";
  if (!std::filesystem::exists(dll_path)) {
    std::wcerr << L"未找到 " << dll_path << L'\n';
    return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
  }

  HRESULT result = RegisterComServer(dll_path);
  if (SUCCEEDED(result)) {
    result = RegisterCategories();
  }
  if (SUCCEEDED(result)) {
    result = RegisterProfile(dll_path);
  }
  return result;
}

HRESULT Uninstall() {
  ComPtr<ITfInputProcessorProfileMgr> profile_manager;
  HRESULT result = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr,
                                    CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(profile_manager.ReleaseAndGetAddressOf()));
  if (SUCCEEDED(result)) {
    result = profile_manager->UnregisterProfile(ziliu::tsf::kTextServiceClsid,
                                                ziliu::tsf::kSimplifiedChineseLanguageId,
                                                ziliu::tsf::kSimplifiedChineseProfileGuid, 0);
  }

  ComPtr<ITfCategoryMgr> category_manager;
  if (SUCCEEDED(CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER,
                                 IID_PPV_ARGS(category_manager.ReleaseAndGetAddressOf())))) {
    constexpr std::array<const GUID*, 4> categories = {
        &GUID_TFCAT_TIP_KEYBOARD,
        &GUID_TFCAT_TIPCAP_UIELEMENTENABLED,
        &GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT,
        &GUID_TFCAT_TIPCAP_SYSTRAYSUPPORT,
    };
    for (const GUID* category : categories) {
      category_manager->UnregisterCategory(ziliu::tsf::kTextServiceClsid, *category,
                                           ziliu::tsf::kTextServiceClsid);
    }
  }

  const std::wstring clsid_key =
      std::wstring(kClsidRoot) + GuidToString(ziliu::tsf::kTextServiceClsid);
  const LSTATUS delete_result = RegDeleteTreeW(HKEY_CURRENT_USER, clsid_key.c_str());
  if (FAILED(result)) {
    return result;
  }
  return delete_result == ERROR_SUCCESS || delete_result == ERROR_FILE_NOT_FOUND
             ? S_OK
             : HRESULT_FROM_WIN32(delete_result);
}

}  // namespace

int wmain(int argument_count, wchar_t** arguments) {
  if (argument_count != 2 ||
      (std::wstring_view(arguments[1]) != L"install" &&
       std::wstring_view(arguments[1]) != L"uninstall")) {
    std::wcerr << L"用法: ZiliuRegister.exe <install|uninstall>\n";
    return 2;
  }

  const HRESULT initialize_result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  if (FAILED(initialize_result)) {
    std::wcerr << L"COM 初始化失败: 0x" << std::hex << initialize_result << L'\n';
    return 3;
  }

  const HRESULT result = std::wstring_view(arguments[1]) == L"install" ? Install() : Uninstall();
  CoUninitialize();
  if (FAILED(result)) {
    std::wcerr << L"操作失败: 0x" << std::hex << result << L'\n';
    return 1;
  }

  std::wcout << L"操作完成。\n";
  return 0;
}
