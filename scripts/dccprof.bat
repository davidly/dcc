@echo off
rem ===========================================================================
rem dccprof.bat - build an app (peep-optimized), run it under ntvcm's per-PC
rem execution-count profiler, and correlate the result against the build's
rem .PRN/.SYM listings (scripts\dccprof.py) into a hot-function summary plus
rem per-line annotated listings.
rem
rem Usage:
rem   scripts\dccprof.bat <app> [-SourcePath FILE] [-BuildDir DIR]
rem                       [-OutDir DIR] [-Clock HZ] [-- program-args...]
rem
rem Delegates the actual build to scripts\ma.ps1 (via pwsh, same as this
rem repo's other Windows orchestration - build-dcc.ps1, runall.ps1) rather
rem than reimplementing the dcc pipeline here; this script only adds the
rem profiling-specific steps ma.ps1 doesn't do: regenerating RTLMIN.PRN
rem (ma.ps1's normal build does not need it, so does not produce it),
rem running the app under ntvcm -g, and invoking dccprof.py.
rem
rem Outputs (in -OutDir, default same as -BuildDir):
rem   <app>_profile_summary.md   - ranked hot-function table
rem   <app>_profile_app.txt      - the app's own .MAC, hit count per line
rem   <app>_profile_rtl.txt      - same, for RTL routines that were hit
rem
rem Examples:
rem   scripts\dccprof.bat tbig
rem   scripts\dccprof.bat tbig -- 20000
rem   scripts\dccprof.bat mm -BuildDir build\profmm
rem ===========================================================================
setlocal EnableExtensions EnableDelayedExpansion

set "prog=%~nx0"

set "app="
set "SourcePath="
set "BuildDir="
set "OutDir="
set "Clock=0"
set "ProgArgs="

:parse
if "%~1"=="" goto parsed
if /I "%~1"=="-h"     goto usage
if /I "%~1"=="--help" goto usage
if /I "%~1"=="-SourcePath" (
    set "SourcePath=%~2"
    shift & shift
    goto parse
)
if /I "%~1"=="-BuildDir" (
    set "BuildDir=%~2"
    shift & shift
    goto parse
)
if /I "%~1"=="-OutDir" (
    set "OutDir=%~2"
    shift & shift
    goto parse
)
if /I "%~1"=="-Clock" (
    set "Clock=%~2"
    shift & shift
    goto parse
)
if "%~1"=="--" (
    shift
    goto collect_prog_args
)
if not defined app (
    set "app=%~1"
    shift
    goto parse
)
echo %prog%: unknown argument: %~1 1>&2
goto usage_err

:collect_prog_args
if "%~1"=="" goto parsed
set "ProgArgs=!ProgArgs! %~1"
shift
goto collect_prog_args

:parsed
if not defined app goto usage_err

set "script_dir=%~dp0"
pushd "%script_dir%.." || (echo %prog%: cannot locate repo root 1>&2 & exit /b 1)
set "repo_root=%CD%"

for /f "usebackq delims=" %%I in (`powershell -NoProfile -Command "'%app%'.ToLower()"`) do set "lower_base=%%I"

if not defined BuildDir set "BuildDir=build\dccprof\%lower_base%"
if not defined OutDir set "OutDir=%BuildDir%"
if not exist "%BuildDir%" mkdir "%BuildDir%"
if not exist "%OutDir%" mkdir "%OutDir%"

if not defined M80C   set "M80C=m80c"
if not defined NTVCM  set "NTVCM=ntvcm"
if not defined PYTHON set "PYTHON=python"

echo === building %app% (peep-optimized) into %BuildDir% ===
set "ma_args=%app% fast -BuildDir "%BuildDir%""
if defined SourcePath set "ma_args=%ma_args% -SourcePath "%SourcePath%""
pwsh -NoProfile -File "%script_dir%ma.ps1" %ma_args%
if errorlevel 1 goto fail

echo === regenerating RTLMIN.PRN (not produced by a normal build) ===
pushd "%BuildDir%"
"%M80C%" "=RTLMIN.MAC" "/X" "/O" "/Z" "/L"
if errorlevel 1 (popd & goto fail)
popd

set "profile_csv=%BuildDir%\%lower_base%_profile.csv"
if exist "%profile_csv%" del "%profile_csv%"

echo === running %lower_base% under the profiler ===
pushd "%BuildDir%"
"%NTVCM%" -p -s:%Clock% -g:"%lower_base%_profile.csv" "%lower_base%.com" %ProgArgs%
popd

if not exist "%profile_csv%" (
    echo %prog%: ntvcm did not produce a profile CSV at %profile_csv% 1>&2
    echo ^(the run may have crashed or been interrupted before exit^) 1>&2
    goto fail
)

echo === correlating profile against listings ===
"%PYTHON%" "%script_dir%dccprof.py" --app "%lower_base%" --build-dir "%BuildDir%" --profile-csv "%profile_csv%" --out-dir "%OutDir%"
if errorlevel 1 goto fail

echo.
echo done: open %OutDir%\%lower_base%_profile_summary.md
popd
exit /b 0

rem :usage and :usage_err are only ever reached during argument parsing,
rem before the repo-root pushd below runs - they must NOT popd. :fail is
rem only ever reached after that pushd succeeded, and always must.
:usage
echo usage: %prog% ^<app^> [-SourcePath FILE] [-BuildDir DIR] [-OutDir DIR] [-Clock HZ] [-- program-args...]
echo(
echo Builds ^<app^> peep-optimized, profiles it under ntvcm, and writes a
echo hot-function summary plus per-line annotated listings.
echo(
echo examples:
echo   %prog% tbig
echo   %prog% tbig -- 20000
echo   %prog% mm -BuildDir build\profmm
exit /b 0

:usage_err
call :usage
exit /b 1

:fail
popd
exit /b 1
