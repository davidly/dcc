@echo off
setlocal
pushd "%~dp0.." || exit /b 1
python scripts\runall.py -mode full -extended --timing-breakdown %*
set "result=%errorlevel%"
popd
exit /b %result%
