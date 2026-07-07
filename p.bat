@echo off
setlocal

if "%~1"=="" (
    call ma.bat tstring
    call ma.bat sieve
    call ma.bat e
    call ma.bat tm
    call ma.bat ttt
    call ma.bat pihex
    call ma.bat mm
    call ma.bat tbig
) else (
    call ma.bat tstring nopeep
    call ma.bat sieve nopeep
    call ma.bat e nopeep
    call ma.bat tm nopeep
    call ma.bat ttt nopeep
    call ma.bat pihex nopeep
    call ma.bat mm nopeep
    call ma.bat tbig nopeep
)

ntvcm -c -p build\tstring
ntvcm -c -p build\sieve
ntvcm -c -p build\e
ntvcm -c -p build\tm
ntvcm -c -p build\ttt 10
ntvcm -c -p build\pihex
ntvcm -c -p build\mm
ntvcm -c -p build\tbig

rem dir /OD tstring.com sieve.com e.com tm.com ttt.com pihex.com mm.com tbig.com

@echo off
for %%i in (tstring.com sieve.com e.com tm.com ttt.com pihex.com mm.com) do (
    for /f "tokens=*" %%a in ('dir /OD build\"%%i" ^| findstr /R "^[0-9]"') do echo %%a
)



