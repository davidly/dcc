@echo off
setlocal

cl /nologo host\dcc0.c host\dcc1.c host\dcc2.c host\dcc3.c host\dcc4.c host\dcc5.c /Ihost /Fedcc.exe /GS- /O2
if errorlevel 1 exit /b 1

for %%m in (dcc0 dcc1 dcc2 x0 x1 x2 x3 dcc4 dcc5) do (
    echo %%m.c
    dcc -S %%m.c -o %%m.mac
    if errorlevel 1 exit /b 1
)

for %%m in (dcc0 dcc1 dcc2 x0 x1 x2 x3 dcc4 dcc5) do (
    echo assembling %%m
    ntvcm m80 =%%m.mac /X /O /Z >%%m.m80 2>&1
    type %%m.m80
    findstr /C:"Fatal error(s)" %%m.m80 | findstr /V /C:"No Fatal" >nul
    if not errorlevel 1 exit /b 1
)

ntvcm m80 =dccrtl.mac /X /O /Z >dccrtl.m80 2>&1
type dccrtl.m80
findstr /C:"Fatal error(s)" dccrtl.m80 | findstr /V /C:"No Fatal" >nul
if not errorlevel 1 exit /b 1

ntvcm l80 dccrtl,dcc0,dcc1,dcc2,x0,x1,x2,x3,dcc4,dcc5,dcc/N/E/Y >dcc.l80 2>&1
type dcc.l80
findstr /C:"?Loading Error" /C:"Mult. Def." /C:"Undefined Global(s)" dcc.l80 >nul
if not errorlevel 1 exit /b 1

if not exist dcc.com exit /b 1

echo Build complete: dcc.com
