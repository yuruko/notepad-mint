@echo off
rem tests\unit\build.bat - builds the unit tests like the app (no crt, /W3, kernel32 / user32 / gdi32 only) and runs them.
rem   tests\unit\build.bat          build build\tests\unit.exe and run it (exit code = the test result)
rem   tests\unit\build.bat norun    build only
rem the tests cover src\util.c src\doc.c src\search.c and src\rt.asm (see unit.c).
setlocal
cd /d "%~dp0..\.."

if defined VSCMD_VER goto haveenv
set "VCVARS=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsamd64_x86.bat"
if exist "%VCVARS%" goto callvc
set "VSW=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%i in (`"%VSW%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VCVARS=%%i\VC\Auxiliary\Build\vcvarsamd64_x86.bat"
:callvc
if not exist "%VCVARS%" echo [unit] could not find vcvarsamd64_x86.bat & exit /b 1
call "%VCVARS%" >nul
:haveenv

if not exist build\tests mkdir build\tests
del /q build\tests\*.obj build\tests\unit.exe 2>nul

set "CFLAGS=/nologo /c /std:c17 /GS- /Zl /Gy /Gw /W3 /utf-8 /O2 /DDOC_IO_TEST /Isrc /Fobuild\tests\\"
set "LFLAGS=/NOLOGO /MACHINE:X86 /SUBSYSTEM:CONSOLE,6.01 /ENTRY:start /NODEFAULTLIB /INCREMENTAL:NO /MANIFEST:NO /SAFESEH:NO /LARGEADDRESSAWARE /OPT:REF /DEBUG:NONE"

echo [unit] asm
ml /nologo /c /Fobuild\tests\rt.obj src\rt.asm || exit /b 1
echo [unit] c
cl %CFLAGS% src\util.c src\doc.c src\search.c src\assoc.c tests\unit\unit.c || exit /b 1
echo [unit] link
link %LFLAGS% /OUT:build\tests\unit.exe build\tests\*.obj kernel32.lib user32.lib gdi32.lib || exit /b 1
echo [unit] built build\tests\unit.exe

if /i "%1"=="norun" exit /b 0
build\tests\unit.exe
exit /b %errorlevel%
