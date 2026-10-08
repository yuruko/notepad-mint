@echo off
rem Build and run isolated no-CRT search/newline benchmarks. Does not touch app/test artifacts.
setlocal
cd /d "%~dp0..\.."
if defined VSCMD_VER goto haveenv
set "VCVARS=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsamd64_x86.bat"
if exist "%VCVARS%" goto callvc
set "VSW=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%i in (`"%VSW%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VCVARS=%%i\VC\Auxiliary\Build\vcvarsamd64_x86.bat"
:callvc
if not exist "%VCVARS%" echo [bench] could not find vcvarsamd64_x86.bat & exit /b 1
call "%VCVARS%" >nul
:haveenv
if not exist build\bench mkdir build\bench
ml /nologo /c /Fobuild\bench\rt.obj src\rt.asm || exit /b 1
ml /nologo /c /Fobuild\bench\baseline.obj tests\bench\baseline.asm || exit /b 1
cl /nologo /c /std:c17 /GS- /Zl /Gy /Gw /W3 /utf-8 /O2 /Isrc /Fobuild\bench\ src\util.c src\search.c tests\bench\perf.c || exit /b 1
link /NOLOGO /MACHINE:X86 /SUBSYSTEM:CONSOLE,6.01 /ENTRY:start /NODEFAULTLIB /INCREMENTAL:NO /MANIFEST:NO /SAFESEH:NO /OPT:REF /OUT:build\bench\perf.exe build\bench\rt.obj build\bench\baseline.obj build\bench\util.obj build\bench\search.obj build\bench\perf.obj kernel32.lib user32.lib gdi32.lib || exit /b 1
build\bench\perf.exe
exit /b %errorlevel%
