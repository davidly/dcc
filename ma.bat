@echo off
setlocal EnableExtensions

if "%~1"=="" (
    echo usage: ma name [peep^|nopeep]
    echo example: ma e peep
    echo example: ma e nopeep
    exit /b 1
)

set "name=%~n1"
set "BUILDDIR=build"
if not exist "%BUILDDIR%" mkdir "%BUILDDIR%"

rem ntvcm resolves COM command files from the current working directory.
rem Since we run ntvcm from build, stage m80/l80 there automatically.
if exist "m80.com" copy /Y "m80.com" "%BUILDDIR%\m80.com" >nul
if exist "l80.com" copy /Y "l80.com" "%BUILDDIR%\l80.com" >nul

set "SOURCE_FILE="

if exist "tests\%name%.c" set "SOURCE_FILE=tests\%name%.c"
if not defined SOURCE_FILE if exist "tests\%name%.C" set "SOURCE_FILE=tests\%name%.C"
if not defined SOURCE_FILE if exist "%name%.c" set "SOURCE_FILE=%name%.c"
if not defined SOURCE_FILE if exist "%name%.C" set "SOURCE_FILE=%name%.C"
set "USE_PEEP=1"

if /I "%~2"=="nopeep" set "USE_PEEP=0"
if /I "%~2"=="nop" set "USE_PEEP=0"
if /I "%~2"=="noopt" set "USE_PEEP=0"
if /I "%~2"=="u" set "USE_PEEP=0"
if /I "%~2"=="-u" set "USE_PEEP=0"
if /I "%~2"=="peep" set "USE_PEEP=1"
if /I "%~2"=="opt" set "USE_PEEP=1"
if /I "%~2"=="-O" set "USE_PEEP=1"

if not defined SOURCE_FILE (
    echo source file not found: tests\%name%.c or %name%.c
    exit /b 1
)

set "DCC_FLAGS="
set "STRIP_FLAGS="

rem Enable float printf only when the source appears to use %f/%F formatting.
rem This keeps ordinary binaries smaller because _pffio and its helpers are not rooted.
findstr /I /C:"%%f" /C:"%%.f" /C:"%%0f" /C:"%%1f" /C:"%%2f" /C:"%%3f" /C:"%%4f" /C:"%%5f" /C:"%%6f" /C:"%%7f" /C:"%%8f" /C:"%%9f" "%SOURCE_FILE%" >nul 2>&1
if not errorlevel 1 (
    set "DCC_FLAGS=%DCC_FLAGS% -ffloatio"
    set "STRIP_FLAGS=%STRIP_FLAGS% -k _pffio"
)

findstr /I /C:"%%F" /C:"%%.F" /C:"%%0F" /C:"%%1F" /C:"%%2F" /C:"%%3F" /C:"%%4F" /C:"%%5F" /C:"%%6F" /C:"%%7F" /C:"%%8F" /C:"%%9F" "%SOURCE_FILE%" >nul 2>&1
if not errorlevel 1 (
    set "DCC_FLAGS=%DCC_FLAGS% -ffloatio"
    set "STRIP_FLAGS=%STRIP_FLAGS% -k _pffio"
)

rem Enable long printf only when the source appears to use %ld/%lu/%lx/%lX/%ls.
rem This keeps ordinary binaries smaller because _pflng and 32-bit helpers are not rooted.
findstr /C:"%%ld" /C:"%%lu" /C:"%%lx" /C:"%%lX" /C:"%%ls" "%SOURCE_FILE%" >nul 2>&1
if not errorlevel 1 (
    set "DCC_FLAGS=%DCC_FLAGS% -flongio"
    set "STRIP_FLAGS=%STRIP_FLAGS% -k _pflng"
)

rem Enable the lightweight stack-overflow guard when the source opts in with a
rem DCC_STACK_CHECK marker, or when DCC_FORCE_STACK_CHECK=1 is set (runall.bat
rem --stack-check guards the whole suite).
if "%DCC_FORCE_STACK_CHECK%"=="1" (
    set "DCC_FLAGS=%DCC_FLAGS% -fstack-check"
) else (
    findstr /C:"DCC_STACK_CHECK" "%SOURCE_FILE%" >nul 2>&1
    if not errorlevel 1 (
        set "DCC_FLAGS=%DCC_FLAGS% -fstack-check"
    )
)

rem DCC_STACK_SIZE overrides the default 512-byte C stack reserve (handy for
rem sweeping stack sizes under -fstack-check).
set "_stack_size=%DCC_STACK_SIZE%"
if "%_stack_size%"=="" set "_stack_size=512"

rem Compile on host first, producing %name%.mac.
dcc.exe %DCC_FLAGS% -stack %_stack_size% "%SOURCE_FILE%" -o "%BUILDDIR%\%name%.mac"
if errorlevel 1 exit /b 1

if "%USE_PEEP%"=="1" (
    dccpeep "%BUILDDIR%\%name%.mac" "%BUILDDIR%\_peepout.mac"
    if errorlevel 1 exit /b 1
    del "%BUILDDIR%\%name%.mac"
    ren "%BUILDDIR%\_peepout.mac" "%name%.mac"
)

rem Ensure CRLF line endings so CP/M M80 doesn't split on embedded LF bytes.
unix2dos "%BUILDDIR%\%name%.mac" >nul 2>nul

rem Assemble app.
pushd "%BUILDDIR%"
ntvcm m80 =%name%.mac /X /O /Z /L
if errorlevel 1 (
    popd
    exit /b 1
)
popd
if errorlevel 1 exit /b 1

rem Produce a subset of the C runtime actually used by the app.
copy /Y DCCRTL.MAC "%BUILDDIR%\DCCRTL.MAC" >nul
unix2dos "%BUILDDIR%\DCCRTL.MAC" >nul 2>nul
dccrtlstrip.exe %STRIP_FLAGS% -r "%BUILDDIR%\DCCRTL.MAC" -o "%BUILDDIR%\rtlmin.mac" "%BUILDDIR%\%name%.mac"
if errorlevel 1 exit /b 1

rem Assemble runtime.
unix2dos "%BUILDDIR%\rtlmin.mac" >nul 2>nul
pushd "%BUILDDIR%"
ntvcm m80 =rtlmin.mac /X /O /Z
if errorlevel 1 (
    popd
    exit /b 1
)
popd
if errorlevel 1 exit /b 1

rem Link app + runtime.
pushd "%BUILDDIR%"
ntvcm l80 /P:100,rtlmin,%name%,%name%/N/E/Y
if errorlevel 1 (
    popd
    exit /b 1
)
popd
if errorlevel 1 exit /b 1



