#include "ziliu/tsf/guids.h"

#include <msctf.h>
#include <windows.h>
#include <aclapi.h>
#include <wrl/client.h>

#include <array>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>

namespace {

using Microsoft::WRL::ComPtr;

constexpr wchar_t kProfileDescription[] = L"字流拼音";
constexpr wchar_t kClsidRoot[] = L"Software\\Classes\\CLSID\\";
constexpr DWORD kInstallLayoutOrTipUninstall = 0x00000001;

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

HRESULT UpdateUserLayoutOrTip(bool install) {
  const HMODULE input_module =
      LoadLibraryExW(L"input.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
  if (input_module == nullptr) {
    return HRESULT_FROM_WIN32(GetLastError());
  }

  using InstallLayoutOrTip = BOOL(CALLBACK*)(LPCWSTR, DWORD);
  const auto install_layout_or_tip = reinterpret_cast<InstallLayoutOrTip>(
      GetProcAddress(input_module, "InstallLayoutOrTip"));
  if (install_layout_or_tip == nullptr) {
    const HRESULT result = HRESULT_FROM_WIN32(GetLastError());
    FreeLibrary(input_module);
    return result;
  }

  const std::wstring profile =
      L"0x0804:" + GuidToString(ziliu::tsf::kTextServiceClsid) +
      GuidToString(ziliu::tsf::kSimplifiedChineseProfileGuid);
  SetLastError(ERROR_SUCCESS);
  const BOOL succeeded = install_layout_or_tip(
      profile.c_str(), install ? 0 : kInstallLayoutOrTipUninstall);
  const DWORD error = GetLastError();
  FreeLibrary(input_module);
  return succeeded ? S_OK
                   : (error == ERROR_SUCCESS ? E_FAIL : HRESULT_FROM_WIN32(error));
}

HRESULT GrantTipLoadAccess(const std::filesystem::path& dll_path) {
  PACL previous = nullptr;
  PSECURITY_DESCRIPTOR descriptor = nullptr;
  const DWORD queried = GetNamedSecurityInfoW(dll_path.c_str(), SE_FILE_OBJECT,
      DACL_SECURITY_INFORMATION, nullptr, nullptr, &previous, nullptr, &descriptor);
  if (queried != ERROR_SUCCESS) {
    return HRESULT_FROM_WIN32(queried);
  }
  // A null DACL already allows access; do not replace it with a package-only ACL.
  if (previous == nullptr) {
    LocalFree(descriptor);
    return S_OK;
  }
  std::array<std::byte, SECURITY_MAX_SID_SIZE> sid_buffer{};
  DWORD sid_size = static_cast<DWORD>(sid_buffer.size());
  if (!CreateWellKnownSid(WinBuiltinAnyPackageSid, nullptr, sid_buffer.data(), &sid_size)) {
    const DWORD error = GetLastError();
    LocalFree(descriptor);
    return HRESULT_FROM_WIN32(error);
  }
  EXPLICIT_ACCESSW access{};
  access.grfAccessPermissions = FILE_GENERIC_READ | FILE_GENERIC_EXECUTE;
  access.grfAccessMode = GRANT_ACCESS;
  access.grfInheritance = NO_INHERITANCE;
  access.Trustee.TrusteeForm = TRUSTEE_IS_SID;
  access.Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
  access.Trustee.ptstrName = reinterpret_cast<wchar_t*>(sid_buffer.data());
  PACL updated = nullptr;
  DWORD result = SetEntriesInAclW(1, &access, previous, &updated);
  if (result == ERROR_SUCCESS) {
    result = SetNamedSecurityInfoW(const_cast<wchar_t*>(dll_path.c_str()), SE_FILE_OBJECT,
        DACL_SECURITY_INFORMATION, nullptr, nullptr, updated, nullptr);
  }
  LocalFree(updated);
  LocalFree(descriptor);
  return HRESULT_FROM_WIN32(result);
}

HRESULT RegisterComServer(const std::filesystem::path& dll_path) {
  const std::wstring key_path =
      std::wstring(kClsidRoot) + GuidToString(ziliu::tsf::kTextServiceClsid) +
      L"\\InprocServer32";
  HKEY key = nullptr;
  const LSTATUS create_result =
      RegCreateKeyExW(HKEY_LOCAL_MACHINE, key_path.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE,
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

  constexpr std::array<const GUID*, 6> categories = {
      &GUID_TFCAT_TIP_KEYBOARD,
      &GUID_TFCAT_TIPCAP_UIELEMENTENABLED,
      &GUID_TFCAT_TIPCAP_INPUTMODECOMPARTMENT,
      &GUID_TFCAT_TIPCAP_COMLESS,
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

HRESULT EnableProfileForCurrentUser() {
  ComPtr<ITfInputProcessorProfiles> profiles;
  const HRESULT result = CoCreateInstance(
      CLSID_TF_InputProcessorProfiles, nullptr, CLSCTX_INPROC_SERVER,
      IID_PPV_ARGS(profiles.ReleaseAndGetAddressOf()));
  return SUCCEEDED(result)
             ? profiles->EnableLanguageProfile(ziliu::tsf::kTextServiceClsid,
                                                ziliu::tsf::kSimplifiedChineseLanguageId,
                                                ziliu::tsf::kSimplifiedChineseProfileGuid, TRUE)
             : result;
}

HRESULT Uninstall();

HRESULT Install() {
  const std::filesystem::path dll_path = ExecutableDirectory() / L"ZiliuTIP.dll";
  if (!std::filesystem::exists(dll_path)) {
    std::wcerr << L"未找到 " << dll_path << L'\n';
    return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
  }

  HRESULT result = GrantTipLoadAccess(dll_path);
  if (FAILED(result)) {
    std::cerr << "GrantTipLoadAccess failed: 0x" << std::hex
              << static_cast<unsigned long>(result) << '\n';
    return result;
  }
  result = RegisterComServer(dll_path);
  if (FAILED(result)) {
    std::cerr << "RegisterComServer failed: 0x" << std::hex
              << static_cast<unsigned long>(result) << '\n';
  }
  if (SUCCEEDED(result)) {
    result = RegisterCategories();
    if (FAILED(result)) {
      std::cerr << "RegisterCategories failed: 0x" << std::hex
                << static_cast<unsigned long>(result) << '\n';
    }
  }
  if (SUCCEEDED(result)) {
    result = RegisterProfile(dll_path);
    if (FAILED(result)) {
      std::cerr << "RegisterProfile failed: 0x" << std::hex
                << static_cast<unsigned long>(result) << '\n';
    }
  }
  if (SUCCEEDED(result)) {
    result = EnableProfileForCurrentUser();
    if (FAILED(result)) {
      std::cerr << "EnableProfileForCurrentUser failed: 0x" << std::hex
                << static_cast<unsigned long>(result) << '\n';
    }
  }
  if (SUCCEEDED(result)) {
    result = UpdateUserLayoutOrTip(true);
    if (FAILED(result)) {
      std::cerr << "UpdateUserLayoutOrTip failed: 0x" << std::hex
                << static_cast<unsigned long>(result) << '\n';
    }
  }
  if (FAILED(result)) {
    const HRESULT install_result = result;
    Uninstall();
    return install_result;
  }
  return result;
}

HRESULT Uninstall() {
  const HRESULT user_layout_result = UpdateUserLayoutOrTip(false);

  ComPtr<ITfCategoryMgr> category_manager;
  if (SUCCEEDED(CoCreateInstance(CLSID_TF_CategoryMgr, nullptr, CLSCTX_INPROC_SERVER,
                                 IID_PPV_ARGS(category_manager.ReleaseAndGetAddressOf())))) {
    constexpr std::array<const GUID*, 6> categories = {
        &GUID_TFCAT_TIP_KEYBOARD,
        &GUID_TFCAT_TIPCAP_UIELEMENTENABLED,
        &GUID_TFCAT_TIPCAP_INPUTMODECOMPARTMENT,
        &GUID_TFCAT_TIPCAP_COMLESS,
        &GUID_TFCAT_TIPCAP_IMMERSIVESUPPORT,
        &GUID_TFCAT_TIPCAP_SYSTRAYSUPPORT,
    };
    for (const GUID* category : categories) {
      category_manager->UnregisterCategory(ziliu::tsf::kTextServiceClsid, *category,
                                           ziliu::tsf::kTextServiceClsid);
    }
  }

  ComPtr<ITfInputProcessorProfileMgr> profile_manager;
  HRESULT result = CoCreateInstance(CLSID_TF_InputProcessorProfiles, nullptr,
                                    CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(profile_manager.ReleaseAndGetAddressOf()));
  if (SUCCEEDED(result)) {
    result = profile_manager->UnregisterProfile(ziliu::tsf::kTextServiceClsid,
                                                ziliu::tsf::kSimplifiedChineseLanguageId,
                                                ziliu::tsf::kSimplifiedChineseProfileGuid, 0);
  }

  const std::wstring clsid_key =
      std::wstring(kClsidRoot) + GuidToString(ziliu::tsf::kTextServiceClsid);
  const LSTATUS delete_result = RegDeleteTreeW(HKEY_LOCAL_MACHINE, clsid_key.c_str());
  if (FAILED(user_layout_result)) {
    return user_layout_result;
  }
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
    std::cerr << "Operation failed: 0x" << std::hex << static_cast<unsigned long>(result)
              << '\n';
    return 1;
  }

  std::wcout << L"操作完成。\n";
  return 0;
}
