@echo off
rem build.bat - builds "notepad mint" (32-bit x86) with the msvc toolchain: no crt, no libs, no windows.h.
rem   build.bat          release build  -> build\notepad mint.exe
rem   build.bat dbg      debug build (symbols + map + build\dbg.log tracing)
rem one 32-bit exe runs on 32-bit windows, 64-bit windows and windows on arm.
setlocal
cd /d "%~dp0"

if defined VSCMD_VER goto haveenv
set "VCVARS=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsamd64_x86.bat"
if exist "%VCVARS%" goto callvc
set "VSW=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%i in (`"%VSW%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VCVARS=%%i\VC\Auxiliary\Build\vcvarsamd64_x86.bat"
:callvc
if not exist "%VCVARS%" echo [build] could not find vcvarsamd64_x86.bat & exit /b 1
call "%VCVARS%" >nul
:haveenv

if not exist build mkdir build
del /q build\*.obj build\*.res "build\notepad mint.exe" 2>nul

set "CFLAGS=/nologo /c /std:c17 /GS- /Zl /Gy /Gw /W3 /utf-8 /Fobuild\\"
set "LFLAGS=/NOLOGO /MACHINE:X86 /SUBSYSTEM:WINDOWS,6.01 /ENTRY:start /NODEFAULTLIB /INCREMENTAL:NO /MANIFEST:NO /SAFESEH:NO /LARGEADDRESSAWARE /OPT:REF /OPT:ICF /MAP:build\notepad_mint.map"
if /i "%1"=="dbg" goto dbgflags
set "CFLAGS=%CFLAGS% /O2"
set "LFLAGS=%LFLAGS% /DEBUG:NONE"
goto flagsdone
:dbgflags
set "CFLAGS=%CFLAGS% /Od /Zi /DDBGLOG /Fdbuild\\vc.pdb"
set "LFLAGS=%LFLAGS% /DEBUG"
:flagsdone

echo [build] asm
ml /nologo /c /Fobuild\rt.obj src\rt.asm || exit /b 1

echo [build] c
if not defined SRCS set "SRCS=src\*.c"
cl %CFLAGS% %SRCS% || exit /b 1

set "RES="
if not exist src\notepad_mint.rc goto nores
echo [build] resources
rc /nologo /fo build\notepad_mint.res src\notepad_mint.rc || exit /b 1
set "RES=build\notepad_mint.res"
:nores

echo [build] link
link %LFLAGS% "/OUT:build\notepad mint.exe" build\*.obj %RES% kernel32.lib user32.lib gdi32.lib || exit /b 1

for %%f in ("build\notepad mint.exe") do echo [build] ok: %%~ff  %%~zf bytes
exit /b 0
