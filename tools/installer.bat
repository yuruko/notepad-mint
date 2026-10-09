@echo off
rem installer.bat - the release files: the NSIS installer and the standalone exe, next to each other in build\.
rem   tools\installer.bat [version]      version defaults to 1.0.12 (keep it in step with FILEVERSION in src\notepad_mint.rc)
rem needs build\notepad-mint.exe (run build.bat first; the release workflow does) and makensis: on the path, in program files, or in
rem build\nsis-<n>\ (the portable zip from nsis.sourceforge.io, unpacked there).
rem output:  build\notepad-mint-<version>.exe          the standalone exe (a copy of build\notepad-mint.exe)
rem          build\notepad-mint-<version>-setup.exe    the installer (installer\notepad-mint.nsi)
rem (every script is called by its absolute path: the current directory is not searched for commands on this setup)
setlocal
cd /d "%~dp0.."
set "ROOT=%CD%"
set "VER=%1"
if "%VER%"=="" set "VER=1.0.12"

if not exist "%ROOT%\build\notepad-mint.exe" echo installer: build\notepad-mint.exe is missing: run build.bat first & exit /b 1

set "MAKENSIS="
where makensis >nul 2>nul && set "MAKENSIS=makensis"
if not defined MAKENSIS if exist "%ProgramFiles(x86)%\NSIS\makensis.exe" set "MAKENSIS=%ProgramFiles(x86)%\NSIS\makensis.exe"
if not defined MAKENSIS if exist "%ProgramFiles%\NSIS\makensis.exe" set "MAKENSIS=%ProgramFiles%\NSIS\makensis.exe"
if not defined MAKENSIS for /d %%d in ("%ROOT%\build\nsis-*") do if exist "%%d\makensis.exe" set "MAKENSIS=%%d\makensis.exe"
if not defined MAKENSIS echo installer: makensis not found (install NSIS 3, or unpack the portable zip to build\nsis-3.10) & exit /b 1

copy /y "%ROOT%\build\notepad-mint.exe" "%ROOT%\build\notepad-mint-%VER%.exe" >nul || exit /b 1
"%MAKENSIS%" /V2 /DVERSION=%VER% "/DEXE_PATH=%ROOT%\build\notepad-mint.exe" "/DOUT_PATH=%ROOT%\build\notepad-mint-%VER%-setup.exe" "/DICON_PATH=%ROOT%\assets\notepad_mint.ico" "%ROOT%\installer\notepad-mint.nsi" || (echo installer: makensis failed & exit /b 1)

for %%f in ("%ROOT%\build\notepad-mint-%VER%.exe" "%ROOT%\build\notepad-mint-%VER%-setup.exe") do echo installer: %%~nxf  %%~zf bytes
exit /b 0
