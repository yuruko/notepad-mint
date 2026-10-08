@echo off
rem Deterministic printing checks: real text layout, intercepted dialog and spooler.
setlocal
cd /d "%~dp0..\.."
if defined VSCMD_VER goto haveenv
set "VCVARS=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsamd64_x86.bat"
if exist "%VCVARS%" goto callvc
set "VSW=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%i in (`"%VSW%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VCVARS=%%i\VC\Auxiliary\Build\vcvarsamd64_x86.bat"
:callvc
if not exist "%VCVARS%" (
  echo [print] could not find vcvarsamd64_x86.bat
  exit /b 1
)
call "%VCVARS%" >nul
:haveenv
if not exist build\print-tests mkdir build\print-tests
del /q build\print-tests\*.obj build\print-tests\print-test.exe 2>nul
ml /nologo /c /Fobuild\print-tests\rt.obj src\rt.asm || exit /b 1
cl /nologo /c /std:c17 /GS- /Zl /Gy /Gw /W3 /utf-8 /O2 /Isrc /Fobuild\print-tests\\ src\util.c tests\print\print_test.c || exit /b 1
link /NOLOGO /MACHINE:X86 /SUBSYSTEM:CONSOLE,6.01 /ENTRY:start /NODEFAULTLIB /INCREMENTAL:NO /MANIFEST:NO /SAFESEH:NO /LARGEADDRESSAWARE /OPT:REF /DEBUG:NONE /OUT:build\print-tests\print-test.exe build\print-tests\*.obj kernel32.lib user32.lib gdi32.lib || exit /b 1
build\print-tests\print-test.exe
exit /b %errorlevel%
