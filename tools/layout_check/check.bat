@echo off
rem check.bat - proves src\w32.h matches the real 32-bit windows sdk (see README.md in this folder).
rem   tools\layout_check\check.bat                  check src\w32.h
rem   tools\layout_check\check.bat path\to\w32.h    check another copy (the negative test uses this)
rem exit code 0 = clean, non-zero = mismatch or tool error. run it from any directory; output goes to build\layout_check.
setlocal
set "W32="
if not "%~1"=="" set "W32=%~f1"
cd /d "%~dp0..\.."
if not defined W32 set "W32=%CD%\src\w32.h"
if not exist "%W32%" echo [layout] no such header: %W32%& goto fail

if defined VSCMD_VER goto haveenv
set "VCVARS=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsamd64_x86.bat"
if exist "%VCVARS%" goto callvc
set "VSW=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%i in (`"%VSW%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VCVARS=%%i\VC\Auxiliary\Build\vcvarsamd64_x86.bat"
:callvc
if not exist "%VCVARS%" echo [layout] could not find vcvarsamd64_x86.bat & goto fail
call "%VCVARS%" >nul
:haveenv

rem the layouts only mean something for the 32-bit target
if not defined VSCMD_ARG_TGT_ARCH goto archok
if /i "%VSCMD_ARG_TGT_ARCH%"=="x86" goto archok
echo [layout] needs the 32-bit (x86) msvc environment, VSCMD_ARG_TGT_ARCH is %VSCMD_ARG_TGT_ARCH%
goto fail
:archok

set "SRCD=tools\layout_check"
set "OUT=build\layout_check"
if not exist "%OUT%" mkdir "%OUT%"
del /q "%OUT%\real_tab.*" "%OUT%\w32_tab.*" "%OUT%\api_tab.*" "%OUT%\cmp.obj" "%OUT%\cmp.exe" "%OUT%\api_names.txt" "%OUT%\api_link.log" "%OUT%\implib.txt" 2>nul

echo [layout] header: %W32%
echo [layout] gen
python "%SRCD%\gen.py" "%W32%" "%OUT%" || goto fail

echo [layout] compile
set "CFLAGS=/nologo /c /std:c17 /GS- /Zl /W3 /utf-8 /I%SRCD%"
cl %CFLAGS% /Fo"%OUT%\real_tab.obj" "%OUT%\real_tab.c" || goto fail
cl %CFLAGS% /Fo"%OUT%\w32_tab.obj" "%OUT%\w32_tab.c" || goto fail
cl %CFLAGS% /Fo"%OUT%\api_tab.obj" "%OUT%\api_tab.c" || goto fail
cl /nologo /std:c17 /W3 /utf-8 /I%SRCD% /Fo"%OUT%\cmp.obj" /Fe"%OUT%\cmp.exe" "%SRCD%\cmp.c" "%OUT%\real_tab.obj" "%OUT%\w32_tab.obj" || goto fail

rem every API prototype must resolve against the import libs: a wrong argument byte count or an unexported name is an
rem unresolved external symbol. the log is read by cmp. (the import lib symbol list is only a hint for the report.)
echo [layout] api link
link /NOLOGO /DLL /NOENTRY /NODEFAULTLIB /MACHINE:X86 /OPT:NOREF /INCREMENTAL:NO /OUT:"%OUT%\api_tab.dll" "%OUT%\api_tab.obj" kernel32.lib user32.lib gdi32.lib > "%OUT%\api_link.log" 2>&1
set "APIRC=%errorlevel%"
if not "%APIRC%"=="0" dumpbin /NOLOGO /LINKERMEMBER:1 kernel32.lib user32.lib gdi32.lib > "%OUT%\implib.txt" 2>&1

echo [layout] compare
"%OUT%\cmp.exe" "%OUT%\api_link.log" %APIRC% "%OUT%\api_names.txt" "%OUT%\implib.txt"
set "RC=%errorlevel%"
if not "%RC%"=="0" goto fail
echo layout_check: clean
exit /b 0

:fail
echo layout_check: FAILED
exit /b 1
