@echo off
setlocal

rem call "c:\program files\microsoft visual studio\18\community\vc\auxiliary\build\vcvars64.bat"
call "c:\program files\microsoft visual studio\2022\community\vc\auxiliary\build\vcvars64.bat"

if exist dcc del /q dcc
if exist dcc.exe del /q dcc.exe
if exist dccpeep del /q dccpeep
if exist dccpeep.exe del /q dccpeep.exe
if exist dccrtlstrip del /q dccrtlstrip
if exist dccrtlstrip.exe del /q dccrtlstrip.exe
if exist dccmake del /q dccmake
if exist dccmake.exe del /q dccmake.exe
if exist m80c del /q m80c
if exist m80c.exe del /q m80c.exe
if exist l80c del /q l80c
if exist l80c.exe del /q l80c.exe

pushd src\dcc
call build-dcc.bat
popd

rem cl /nologo dcc.c /GS- /GL /Oti2 /Ob3 /Qpar /Fa /FAsc /EHac /Zi /D_AMD64_ /link user32.lib ntdll.lib /OPT:REF

cl /nologo src\dccpeep\*.c /I src\dccpeep /GS- /GL /Oti2 /Ob3 /Qpar /Fa /FAsc /EHac /Zi /std:c11 /D_AMD64_ /link user32.lib ntdll.lib /OPT:REF

cl /nologo src\dccrtlstrip\dccrtlstrip.c /GS- /GL /Oti2 /Ob3 /Qpar /Fa /FAsc /EHac /Zi /D_AMD64_ /link user32.lib ntdll.lib /OPT:REF

cl /nologo src\dccmake\dccmake.c /GS- /GL /Oti2 /Ob3 /Qpar /Fa /FAsc /EHac /Zi /D_AMD64_ /link user32.lib ntdll.lib /OPT:REF

cl /nologo src\m80c\m80c.c /GS- /GL /Oti2 /Ob3 /Qpar /Fa /FAsc /EHac /Zi /D_AMD64_ /link user32.lib ntdll.lib /OPT:REF

cl /nologo src\l80c\l80c.c /GS- /GL /Oti2 /Ob3 /Qpar /Fa /FAsc /EHac /Zi /D_AMD64_ /link user32.lib ntdll.lib /OPT:REF

