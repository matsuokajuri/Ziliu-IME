@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "BUILD_CONFIG=%~1"
if not defined BUILD_CONFIG set "BUILD_CONFIG=Debug"
if /I not "!BUILD_CONFIG!"=="Debug" if /I not "!BUILD_CONFIG!"=="Release" (
  echo Usage: %~nx0 [Debug^|Release]
  exit /b 2
)
if not "%~2"=="" (
  echo Usage: %~nx0 [Debug^|Release]
  exit /b 2
)
set "BUILD_DIR=%~dp0..\build\local-x64-!BUILD_CONFIG!"
if not defined ZILIU_DEPENDENCY_ROOT set "ZILIU_DEPENDENCY_ROOT=%~dp0.."

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo Visual Studio Installer was not found.
  exit /b 1
)

set "VS_INSTALL="
for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -products * -property installationPath`) do (
  set "VS_INSTALL=%%I"
)

if not defined VS_INSTALL (
  echo Visual Studio was not found.
  exit /b 1
)

call "%VS_INSTALL%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
if errorlevel 1 exit /b %errorlevel%

rem Visual Studio 18 can expose the compiler payload before its VC environment
rem component is registered. Build a deterministic environment in that case.
where nmake.exe >nul 2>nul
if errorlevel 1 (
  set "VC_TOOLS_VERSION="
  for /f "delims=" %%I in ('dir /b /ad /o-n "%VS_INSTALL%\VC\Tools\MSVC"') do if not defined VC_TOOLS_VERSION set "VC_TOOLS_VERSION=%%I"
  set "SDK_VERSION="
  for /f "delims=" %%I in ('dir /b /ad /o-n "%ProgramFiles(x86)%\Windows Kits\10\Include"') do if not defined SDK_VERSION set "SDK_VERSION=%%I"

  if not defined VC_TOOLS_VERSION (
    echo MSVC compiler payload was not found.
    exit /b 1
  )
  if not defined SDK_VERSION (
    echo Windows SDK was not found.
    exit /b 1
  )

  set "VC_TOOLS=!VS_INSTALL!\VC\Tools\MSVC\!VC_TOOLS_VERSION!"
  set "WINSDK=!ProgramFiles(x86)!\Windows Kits\10"
  set "PATH=!VC_TOOLS!\bin\Hostx64\x64;!WINSDK!\bin\!SDK_VERSION!\x64;!PATH!"
  set "INCLUDE=!VC_TOOLS!\include;!WINSDK!\Include\!SDK_VERSION!\ucrt;!WINSDK!\Include\!SDK_VERSION!\shared;!WINSDK!\Include\!SDK_VERSION!\um;!WINSDK!\Include\!SDK_VERSION!\winrt;!WINSDK!\Include\!SDK_VERSION!\cppwinrt"
  set "LIB=!VC_TOOLS!\lib\x64;!WINSDK!\Lib\!SDK_VERSION!\ucrt\x64;!WINSDK!\Lib\!SDK_VERSION!\um\x64"
)

if not defined VC_TOOLS if defined VCToolsInstallDir set "VC_TOOLS=!VCToolsInstallDir!"

if not exist "!VC_TOOLS!\include\iostream" (
  echo The MSVC C++ standard library is missing.
  echo Install Visual Studio component Microsoft.VisualStudio.Component.VC.Tools.x86.x64.
  exit /b 1
)
if not exist "!VC_TOOLS!\lib\x64\msvcrt.lib" (
  echo The MSVC C++ runtime libraries are missing.
  echo Install Visual Studio component Microsoft.VisualStudio.Component.VC.Tools.x86.x64.
  exit /b 1
)

cmake --fresh -S "%~dp0.." -B "!BUILD_DIR!" -G "NMake Makefiles" ^
  -DCMAKE_BUILD_TYPE=!BUILD_CONFIG! -DZILIU_BUILD_TESTS=ON -DZILIU_ENABLE_RIME=ON ^
  "-DZILIU_DEPENDENCY_ROOT=!ZILIU_DEPENDENCY_ROOT!"
if errorlevel 1 exit /b %errorlevel%

cmake --build "!BUILD_DIR!"
if errorlevel 1 exit /b %errorlevel%

ctest --test-dir "!BUILD_DIR!" --output-on-failure
exit /b %errorlevel%
