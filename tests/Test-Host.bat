@echo off
setlocal DisableDelayedExpansion
where pwsh.exe >nul 2>nul
if errorlevel 1 goto legacy
pwsh.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0Test-Host.ps1" %*
exit /b %errorlevel%
:legacy
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0Test-Host.ps1" %*
exit /b %errorlevel%
