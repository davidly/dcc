@echo off
setlocal

if "%~1"=="" (
    echo usage: mau name
    echo example: mau e
    exit /b 1
)

set name=%~n1

rem Compile on host first, producing %name%.mac or %name%.asm.
dcc %name%.c -o %name%.mac
if errorlevel 1 exit /b 1

rem Ensure CRLF line endings so CP/M M80 doesn't split on embedded LF bytes.
unix2dos %name%.mac >nul 2>nul

rem Assemble app.
ntvcm m80 =%name%.mac /X /O /Z /L
if errorlevel 1 exit /b 1

rem Assemble runtime
unix2dos dccrtl.mac >nul 2>nul
ntvcm m80 =dccrtl.mac /X /O /Z
if errorlevel 1 exit /b 1

rem Link app + runtime.
ntvcm l80 dccrtl,%name%,%name%/N/E/Y
if errorlevel 1 exit /b 1



