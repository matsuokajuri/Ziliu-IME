@echo off
setlocal EnableExtensions

powershell.exe -NoProfile -ExecutionPolicy Bypass ^
  -File "%~dp0deploy-settings-bundle.ps1" %*
exit /b %errorlevel%
