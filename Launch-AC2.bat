@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Launch-AC2.ps1" %*
if errorlevel 1 (
  echo AC2 could not start. See the error above.
  if "%~1"=="" pause
  exit /b 1
)
exit /b 0
