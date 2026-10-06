@echo off
rem cc.bat - syntax/compile check of ONE source file (32-bit x86) without touching the real build (no link, own output dir).
rem   tools\cc.bat find.c        (file name relative to src\; run from anywhere)
setlocal
if "%~1"=="" echo usage: cc.bat file.c & exit /b 1
if defined VSCMD_VER goto haveenv
set "VCVARS=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsamd64_x86.bat"
if not exist "%VCVARS%" echo [cc] vcvarsamd64_x86.bat not found & exit /b 1
call "%VCVARS%" >nul
:haveenv
cd /d "%~dp0.."
if not exist build\cc mkdir build\cc
cl /nologo /c /std:c17 /GS- /Zl /Gy /W3 /utf-8 /Fobuild\cc\ src\%~1
exit /b %errorlevel%
