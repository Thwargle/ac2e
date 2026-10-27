@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Start-AC2E.ps1" %*
if errorlevel 1 pause
