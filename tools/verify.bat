@echo off
rem verify.bat - every automatic check in one go. stops at the first failure; the last line says what happened.
rem   tools\verify.bat          release build (zero warnings), imports, layout check, unit tests, smoke test
rem   tools\verify.bat ui       ... and the message-driven gui tests (tests\ui\ui_test.ps1)
rem   tools\verify.bat frame    ... and the title strip test with real mouse input (tools\frame_test.ps1: moves the mouse for a few seconds)
rem the smoke / gui / frame tests start the exe on the real desktop (a window flashes up); they need a desktop session.
rem (every script is called by its absolute path: the current directory is not searched for commands on this setup)
setlocal
cd /d "%~dp0.."
set "ROOT=%CD%"
if not exist build mkdir build

echo [verify] build (release)
cmd /c ""%ROOT%\build.bat"" > build\verify-build.log 2>&1
if errorlevel 1 type build\verify-build.log & echo verify: BUILD FAILED & exit /b 1
findstr /r /c:"warning [A-Z][A-Z]*[0-9][0-9]*" build\verify-build.log && (echo verify: THE BUILD PRINTED WARNINGS & exit /b 1)

rem the exe has to stay under 300000 bytes (the maintainer's rule: notepad size)
for %%f in ("%ROOT%\build\notepad-mint.exe") do set "EXESIZE=%%~zf"
if %EXESIZE% GEQ 300000 echo verify: THE EXE IS %EXESIZE% BYTES, OVER THE 300000 BYTE LIMIT & exit /b 1
echo [verify] exe size %EXESIZE% bytes (limit 300000)

echo [verify] imports
powershell -NoProfile -File "%ROOT%\tools\check_imports.ps1" || (echo verify: IMPORTS FAILED & exit /b 1)

echo [verify] layout check
cmd /c ""%ROOT%\tools\layout_check\check.bat"" > build\verify-layout.log 2>&1
if errorlevel 1 type build\verify-layout.log & echo verify: LAYOUT CHECK FAILED & exit /b 1
findstr /b /c:"layout_check:" /c:"structs:" /c:"fields:" /c:"constants:" /c:"api:" build\verify-layout.log

echo [verify] unit tests
cmd /c ""%ROOT%\tests\unit\build.bat"" > build\verify-unit.log 2>&1
if errorlevel 1 type build\verify-unit.log & echo verify: UNIT TESTS FAILED & exit /b 1
findstr /b /c:"unit:" build\verify-unit.log

echo [verify] smoke test
powershell -NoProfile -File "%ROOT%\tools\smoke.ps1" -Wait 3 || (echo verify: SMOKE TEST FAILED & exit /b 1)

if /i "%1"=="ui" goto ui
if /i "%1"=="frame" goto frame
goto done
:ui
echo [verify] gui tests
powershell -NoProfile -File "%ROOT%\tests\ui\ui_test.ps1" || (echo verify: GUI TESTS FAILED & exit /b 1)
echo [verify] selected line breaks, the partly visible rows, the theme button's hover and the button tooltips (probe build: /DSHOTDC dumps the editor's own pixels, /DMENU_NO_TRACK keeps a synthetic hover)
cmd /c ""%ROOT%\tools\probe.bat" /DSHOTDC /DMENU_NO_TRACK" > build\verify-probe.log 2>&1
if errorlevel 1 type build\verify-probe.log & echo verify: PROBE BUILD FAILED & exit /b 1
powershell -NoProfile -File "%ROOT%\tests\ui\ui_test.ps1" -Exe "%ROOT%\build\probe\notepad-mint.exe" -Only T23,T25,T28,T30 || (echo verify: DIRECT PAINT TEST FAILED & exit /b 1)
goto done
:frame
echo [verify] title strip test
powershell -NoProfile -File "%ROOT%\tools\frame_test.ps1" -Exe "%ROOT%\build\notepad-mint.exe" || (echo verify: FRAME TEST FAILED & exit /b 1)
:done
echo verify: all passed
exit /b 0
