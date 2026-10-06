@echo off
rem cc_asm.bat - compile ONE file at /O2 and keep the assembly listing:  build\cc\<name>.asm
setlocal
if "%~1"=="" echo usage: cc_asm.bat file.c & exit /b 1
if defined VSCMD_VER goto haveenv
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsamd64_x86.bat" >nul
:haveenv
cd /d "%~dp0.."
if not exist build\cc mkdir build\cc
cl /nologo /c /std:c17 /GS- /Zl /Gy /Gw /W3 /utf-8 /O2 /FAs /Fabuild\cc\ /Fobuild\cc\ src\%~1
exit /b %errorlevel%
