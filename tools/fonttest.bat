@echo off
setlocal
if defined VSCMD_VER goto haveenv
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsamd64_x86.bat" >nul
:haveenv
cd /d "%~dp0.."
if not exist build\ft mkdir build\ft
ml /nologo /c /Fobuild\ft\rt.obj src\rt.asm || exit /b 1
cl /nologo /c /std:c17 /GS- /Zl /Gy /W3 /utf-8 /O2 /Fobuild\ft\ tools\fonttest.c || exit /b 1
link /NOLOGO /MACHINE:X86 /SUBSYSTEM:WINDOWS /ENTRY:start /NODEFAULTLIB /SAFESEH:NO /OUT:build\ft\fonttest.exe build\ft\fonttest.obj build\ft\rt.obj kernel32.lib user32.lib gdi32.lib || exit /b 1
echo built build\ft\fonttest.exe
