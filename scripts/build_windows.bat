@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
set "PS_SCRIPT=%SCRIPT_DIR%build_windows.ps1"

if not exist "%PS_SCRIPT%" (
  echo [windows] ERROR: %PS_SCRIPT% not found
  exit /b 1
)

powershell -NoProfile -ExecutionPolicy Bypass -File "%PS_SCRIPT%" %*
if errorlevel 1 exit /b %errorlevel%

endlocal
