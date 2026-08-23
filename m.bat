@echo off
setlocal

where pwsh >nul 2>nul
if errorlevel 1 (
	echo PowerShell 7 ^(pwsh^) is required to run scripts/build-dcc.ps1. 1>&2
	exit /b 1
)

pwsh -NoProfile -File "%~dp0scripts\build-dcc.ps1" %*
set "exit_code=%errorlevel%"
exit /b %exit_code%

