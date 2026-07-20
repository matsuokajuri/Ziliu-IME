@echo off
setlocal EnableExtensions

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VS_INSTALL="
for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -products * -property installationPath`) do set "VS_INSTALL=%%I"
if not defined VS_INSTALL exit /b 1

set "CONFIGURATION=%~1"
if not defined CONFIGURATION set "CONFIGURATION=Release"
set "OUTPUT_DIR=%~2"
if not defined OUTPUT_DIR set "OUTPUT_DIR=%~dp0..\build\winui3-output\"

"%VS_INSTALL%\MSBuild\Current\Bin\amd64\MSBuild.exe" "%~dp0..\src\settings\ZiliuSettings.vcxproj" /nologo /restore /m /v:minimal /p:Configuration=%CONFIGURATION% /p:Platform=x64 "/p:ZiliuOutputDir=%OUTPUT_DIR%"
exit /b %errorlevel%
