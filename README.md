# notepad mint

a free, dark replacement for `notepad.exe`: plain text only. no ai, no sign-in, no telemetry.

- written in raw c and **32-bit x86 assembly**. no crt, no libraries, no `windows.h`: every win32 declaration is hand-written in `src/w32.h`
  (checked against the real sdk by `tools/layout_check`). the exe imports only kernel32 / user32 / gdi32 (everything else is loaded at run time).
- one small 32-bit exe runs on 32-bit windows, 64-bit windows and windows on arm.
- behaves like notepad (same menus, status bar and wording) plus: a dark theme (the default) and a light one, both with a mint accent (`#9df5bd`);
  title bar, menu bar and status bar in the editor's own font (3 pt smaller); ctrl+plus / ctrl+minus / ctrl+0 font-size zoom (10-96 pt);
  line ending and encoding pickers; utf-8 / utf-16 / legacy code pages; right-to-left toggle; unicode control characters; drag and drop; per-monitor dpi;
  an unsaved document is called `mintXXXX` (4 characters of 0-9 a-z from the date and time) instead of "untitled".

## status

every menu item works: file (new, new window, open, save, save as, page setup, print, exit), edit (undo ... select all, time / date, find, find next / previous,
replace, go to), format (word wrap, font, line ending, encoding), view (zoom, status bar, theme), help. the dialogs are our own dark / light ones (open, save as,
find / replace, go to, font, encodings); print and page setup are the native windows dialogs (light).

how each part was verified, and what is still open, is in `NOTES.md` (status) and `TODO.md`.

## build

needs visual studio's c++ build tools (x86 target) and the windows sdk (python 3 for the layout check).

    build.bat                 release build -> build\notepad mint.exe (zero warnings at /W3)
    build.bat dbg             debug build (symbols, build\dbg.log tracing)

`build.bat` looks for `vcvarsamd64_x86.bat` of visual studio 18 community and falls back to `vswhere`.

## checks

    tools\verify.bat          release build + warning scan, imports, layout check, unit tests, smoke test
    tools\verify.bat ui       ... plus the message-driven gui tests (tests\ui\ui_test.ps1)
    tools\verify.bat frame    ... plus the title strip test (tools\frame_test.ps1: real mouse input, it moves the mouse for a few seconds)

github ci is switched off (only the cloud agent needs it): the workflow with the same steps (minus the gui and title strip tests) is parked as `.github/workflows/build.yml.disabled`, rename it to `build.yml` to turn it on.
`tools\` also has `shot.ps1` (launch, send keys or a menu command, screenshot), `cc.bat` (compile-check one file), `probe.bat` (experimental build) and `make_icon.ps1` (builds the icon).

## notes

the app icon is the real notepad icon recolored mint by `tools/make_icon.ps1` (it reads it from the local `notepad.exe`),
so it is derived from microsoft's artwork. there is no license file yet.
