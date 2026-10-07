@echo off
rem probe.bat - builds an experimental copy of the app into build\probe\ without touching the real build (build\*.obj, build\notepad-mint.exe).
rem   tools\probe.bat /DFRAME_CUSTOM=1        extra cl switches are passed straight to cl (macros, /O2 ...)
rem the exe is build\probe\notepad-mint.exe (same file name, so tools\shot.ps1 -Exe ... also kills it afterwards).
setlocal
cd /d "%~dp0.."

if defined VSCMD_VER goto haveenv
set "VCVARS=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsamd64_x86.bat"
if not exist "%VCVARS%" echo [probe] could not find vcvarsamd64_x86.bat & exit /b 1
call "%VCVARS%" >nul
:haveenv

if not exist build\probe mkdir build\probe
del /q build\probe\*.obj build\probe\*.res build\probe\notepad-mint.exe 2>nul

set "CFLAGS=/nologo /c /std:c17 /GS- /Zl /Gy /Gw /W3 /utf-8 /O1 /Fobuild\probe\\"
set "LFLAGS=/NOLOGO /MACHINE:X86 /SUBSYSTEM:WINDOWS,6.01 /ENTRY:start /NODEFAULTLIB /INCREMENTAL:NO /MANIFEST:NO /SAFESEH:NO /LARGEADDRESSAWARE /OPT:REF /OPT:ICF /DEBUG:NONE"

ml /nologo /c /Fobuild\probe\rt.obj src\rt.asm || exit /b 1
cl %CFLAGS% %* src\*.c || exit /b 1
rc /nologo /fo build\probe\notepad_mint.res src\notepad_mint.rc || exit /b 1
link %LFLAGS% /OUT:build\probe\notepad-mint.exe build\probe\*.obj build\probe\notepad_mint.res kernel32.lib user32.lib gdi32.lib || exit /b 1
for %%f in (build\probe\notepad-mint.exe) do echo [probe] ok: %%~ff  %%~zf bytes
exit /b 0
