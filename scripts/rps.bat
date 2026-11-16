@echo off
setlocal
pushd "%~dp0.." || exit /b 1
pwsh -NoProfile -File scripts\runall.ps1 -mode full -extended -ti %*
set "result=%errorlevel%"
popd
exit /b %result%
