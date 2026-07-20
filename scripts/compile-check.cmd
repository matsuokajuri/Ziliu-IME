@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VS_INSTALL="
for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -products * -property installationPath`) do set "VS_INSTALL=%%I"
if not defined VS_INSTALL exit /b 1

set "VC_TOOLS_VERSION="
for /f "delims=" %%I in ('dir /b /ad /o-n "%VS_INSTALL%\VC\Tools\MSVC"') do if not defined VC_TOOLS_VERSION set "VC_TOOLS_VERSION=%%I"
set "SDK_VERSION="
for /f "delims=" %%I in ('dir /b /ad /o-n "%ProgramFiles(x86)%\Windows Kits\10\Include"') do if not defined SDK_VERSION set "SDK_VERSION=%%I"
if not defined VC_TOOLS_VERSION exit /b 1
if not defined SDK_VERSION exit /b 1

set "VC_TOOLS=!VS_INSTALL!\VC\Tools\MSVC\!VC_TOOLS_VERSION!"
set "WINSDK=!ProgramFiles(x86)!\Windows Kits\10"
set "PATH=!VC_TOOLS!\bin\Hostx64\x64;!WINSDK!\bin\!SDK_VERSION!\x64;!PATH!"
set "INCLUDE=!VC_TOOLS!\include;!WINSDK!\Include\!SDK_VERSION!\ucrt;!WINSDK!\Include\!SDK_VERSION!\shared;!WINSDK!\Include\!SDK_VERSION!\um;!WINSDK!\Include\!SDK_VERSION!\winrt;!WINSDK!\Include\!SDK_VERSION!\cppwinrt"

if not exist "!VC_TOOLS!\include\iostream" (
  echo The MSVC C++ standard library is missing.
  echo Install Visual Studio component Microsoft.VisualStudio.Component.VC.Tools.x86.x64.
  exit /b 1
)

set "ROOT=%~dp0.."
set "OBJDIR=!ROOT!\build\compile-check"
if not exist "!OBJDIR!" mkdir "!OBJDIR!"

set "COMMON=/nologo /c /std:c++latest /W4 /WX /permissive- /utf-8 /Zc:__cplusplus /Zc:preprocessor /EHsc /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DNOMINMAX /D_WIN32_WINNT=0x0A00 /DWINVER=0x0A00"

call :compile "!ROOT!\src\core\src\stub_engine.cpp" core_stub /I"!ROOT!\src\core\include"
if errorlevel 1 exit /b !errorlevel!
call :compile "!ROOT!\src\core\src\ipc_protocol.cpp" core_protocol /I"!ROOT!\src\core\include"
if errorlevel 1 exit /b !errorlevel!
call :compile "!ROOT!\src\core\src\session_host.cpp" core_session_host /I"!ROOT!\src\core\include"
if errorlevel 1 exit /b !errorlevel!
call :compile "!ROOT!\src\ipc\src\pipe_client.cpp" ipc_client /I"!ROOT!\src\core\include" /I"!ROOT!\src\ipc\include"
if errorlevel 1 exit /b !errorlevel!
call :compile "!ROOT!\src\ipc\src\pipe_server.cpp" ipc_server /I"!ROOT!\src\core\include" /I"!ROOT!\src\ipc\include"
if errorlevel 1 exit /b !errorlevel!
call :compile "!ROOT!\src\ui\src\candidate_window.cpp" ui_candidate /I"!ROOT!\src\core\include" /I"!ROOT!\src\ui\include"
if errorlevel 1 exit /b !errorlevel!
call :compile "!ROOT!\src\tsf\src\text_service.cpp" tsf_service /I"!ROOT!\src\core\include" /I"!ROOT!\src\ipc\include" /I"!ROOT!\src\tsf\include" /I"!ROOT!\src\ui\include"
if errorlevel 1 exit /b !errorlevel!
call :compile "!ROOT!\src\tsf\src\dll_main.cpp" tsf_dll /I"!ROOT!\src\tsf\include"
if errorlevel 1 exit /b !errorlevel!
call :compile "!ROOT!\src\broker\src\broker_main.cpp" broker /I"!ROOT!\src\core\include" /I"!ROOT!\src\ipc\include" /I"!ROOT!\src\broker\include"
if errorlevel 1 exit /b !errorlevel!
call :compile "!ROOT!\src\broker\src\rime_engine.cpp" broker_rime /I"!ROOT!\src\core\include" /I"!ROOT!\src\broker\include" /I"!ROOT!\third_party\librime\src"
if errorlevel 1 exit /b !errorlevel!
call :compile "!ROOT!\src\settings\src\settings_main.cpp" settings
if errorlevel 1 exit /b !errorlevel!
call :compile "!ROOT!\tools\register\register_main.cpp" register /I"!ROOT!\src\tsf\include"
if errorlevel 1 exit /b !errorlevel!
call :compile "!ROOT!\tests\core_tests.cpp" core_tests /I"!ROOT!\src\core\include"
if errorlevel 1 exit /b !errorlevel!
call :compile "!ROOT!\tests\ipc_tests.cpp" ipc_tests /I"!ROOT!\src\core\include" /I"!ROOT!\src\ipc\include"
if errorlevel 1 exit /b !errorlevel!
call :compile "!ROOT!\tests\rime_engine_tests.cpp" rime_engine_tests /I"!ROOT!\src\core\include" /I"!ROOT!\src\broker\include"
if errorlevel 1 exit /b !errorlevel!

echo compile-check: OK
exit /b 0

:compile
set "SOURCE=%~1"
set "NAME=%~2"
echo Checking !NAME!...
cl !COMMON! %~3 %~4 %~5 %~6 /Fo"!OBJDIR!\!NAME!.obj" "!SOURCE!"
exit /b !errorlevel!
