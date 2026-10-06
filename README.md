# notepad mint

a free, dark replacement for `notepad.exe`: plain text only. no ai, no sign-in, no telemetry.

- written in raw c and **32-bit x86 assembly**. no crt, no libraries, no `windows.h`: every win32 declaration is hand-written in `src/w32.h`.
  the exe imports only kernel32 / user32 / gdi32 (everything else is loaded at run time).
- one small 32-bit exe runs on 32-bit windows, 64-bit windows and windows on arm.
- behaves like notepad (same menus, status bar and wording) plus: dark theme with a mint accent (`#9df5bd`), custom text / background colors,
  menu + status bar in the editor's own font, ctrl+plus / ctrl+minus / ctrl+0 font-size zoom (10-96 pt), line ending and encoding pickers,
  utf-8 / utf-16 / legacy code pages, right-to-left toggle, unicode control characters, drag and drop, per-monitor dpi.

## status: work in progress

works: the main window, menus, status bar, word wrap, font-size zoom, about + help, opening files from the command line or by drag and drop,
saving in place (ctrl+s) , the 32-bit build.

not written yet (the menu items show an "isn't written yet" message): find / replace / go to, the open and save as dialogs,
the encoding pickers, the font & colors dialog, print and page setup. see `NOTES.md` for the design, the decisions and the exact to-do list.

## build

needs visual studio's c++ build tools (x86 target) and the windows sdk.

    build.bat          release build -> build\notepad mint.exe
    build.bat dbg      debug build (symbols, build\dbg.log tracing)

`build.bat` looks for `vcvarsamd64_x86.bat` of visual studio 18 community and falls back to `vswhere`.
`tools\` has helpers: `shot.ps1` (launch, send keys, screenshot), `cc.bat` (compile-check one file), `make_icon.ps1` (builds the icon).

## notes

the app icon is the real notepad icon recolored mint by `tools/make_icon.ps1` (it reads it from the local `notepad.exe`),
so it is derived from microsoft's artwork. there is no license file yet.
