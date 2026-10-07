# notepad mint

a free, dark replacement for `notepad.exe`: plain text only. no ai, no sign-in, no telemetry.

| dark (the default) | light |
|---|---|
| ![notepad mint, dark theme](assets/screenshot-dark.png) | ![notepad mint, light theme](assets/screenshot-light.png) |

## download

from the [latest release](https://github.com/yuruko/notepad-mint/releases/latest):

- `notepad-mint-<version>-setup.exe`: the installer (installs to `program files (x86)\notepad-mint`; start menu shortcut, optional desktop shortcut, uninstaller in *installed apps*; your settings are left alone when you uninstall)
- `notepad-mint-<version>.exe`: the standalone exe, nothing to install: put it anywhere and run it

one 32-bit exe (about 140 kb) runs on 32-bit windows, 64-bit windows and windows on arm. settings are in `%appdata%\notepad-mint\settings.ini`.

## what it does

- written in raw c and **32-bit x86 assembly**. no crt, no libraries, no `windows.h`: every win32 declaration is hand-written in `src/w32.h`
  (checked against the real sdk by `tools/layout_check`). the exe imports only kernel32 / user32 / gdi32 (everything else is loaded at run time).
- behaves like notepad (same menus, status bar and wording) plus:
  - **themes**: a dark theme (the default, mint accent `#9df5bd`) and a light one (a softer green accent `#2f7d58`); alt+x or the sun / moon button at the very right of the menu bar switches them; hover uses the accent, the caret is plain white / black;
    title bar (white / black over a soft accent fade), menu bar and status bar in the editor's font face at a fixed 11 px; classic style scrollbars in both themes' colours, 13 px thin;
  - **word wrap**: alt+z, or the button left of the theme button; hover either button for a moment and a tooltip says what it does and its key combo;
  - **tab size**: format > tab size: 2, 4 or 8 columns (4 by default), remembered, also used when printing;
  - **recent files**: file > recent keeps the last 9 files you opened or saved (newest first, press 1-9), shared by all open windows; "clear list" at the bottom empties it;
  - **text runs to the edge**: an 8 px margin around the text (4 px on top), but text that is scrolled out of it runs to the very edge of the editor, no blank frame; the row that is only partly in view at the bottom is drawn, cut off at the edge;
  - **status bar**: compact (`12:5`, `5 L`, `124 B`, `crlf`, `utf8 bom`), the lines, bytes and line ending panels grow and shrink with their text, never cut off; with a selection it shows the lines and bytes selected (`162:54 [5 L 54 B]`); click the line ending / encoding panels to change them;
  - **zoom**: ctrl+plus / ctrl+minus / ctrl+0, ctrl + mouse wheel (10-96 pt);
  - **encodings and line endings**: utf-8 (with or without bom), utf-16 le / be, legacy code pages, reopen with another encoding; windows / unix / classic mac line endings; right-to-left toggle; unicode control characters; per-monitor dpi;
  - **drop files or folders** on the window to insert their paths at the caret, one per line (shift+drop opens the files instead); a selected line break shows as a small highlighted block, so empty lines and line ends inside a selection can be seen;
  - **ctrl+k** clears the current line (ctrl+z brings it back); ctrl+backspace / ctrl+delete delete a word;
  - an unsaved document is called `mint-XXXX` (4 characters of 0-9 a-z from the date and time) instead of "untitled"; "modified" means *different from the file* (type and delete again, or undo back, is not a change);
  - the window can be made small: down to 320 x 140 px.

## small, fast, clean

- **small**: the whole editor is one 139 kb exe (it was 210 kb before the cleanup; the hard limit for this project is 300 kb). the biggest saving was the 256 px icon, 65 kb of png turned into a 14 kb palette png with an ordered dither (`tools/shrink_icon.py`), then a size-optimised build (`/O1`). what is left is about 71 kb of code, 19 kb of text and tables, 34 kb of resources (the icon, the manifest, the version info) and a few kb of import data. no crt, no libraries, no third-party code: the imports are kernel32 / user32 / gdi32 and nothing else (checked on every build by `tools/check_imports.ps1`).
- **fast**: it uses the stock windows edit control, so typing and scrolling are as quick as notepad's, and big files open quickly:
  - opening converts the file once (no size query pass) and gives back what multi-byte text did not need; saving converts utf-8 in one pass, writes straight from the editor's own buffer (no 40 mb copy of a 20 mb file) and skips the line-ending rewrite when the text already is crlf;
  - the status bar caches the caret's line / column per keystroke instead of rescanning the text several times, and on a text over a million characters the line / byte counts follow when typing pauses instead of on every key;
  - find ignores non-ascii text without a call into windows per character, `memcmp` compares a dword at a time, `memmove` copies dwords backwards too, and `mp_count_lf` (line counting) is sse2 assembly;
  - everything the window draws itself (the rows that are only partly in view, the scrolled-out text in the margin) is drawn off screen and put on in one blit, so a resize or a scroll does not flicker.
- **clean**: zero warnings at `/W3`; `tools/layout_check` proves every struct, constant and function in the hand-written `src/w32.h` against the real windows sdk (the header was pruned of 111 declarations nothing used); an audit found no unused function, and the notes that only described how the code used to be are gone; 399 unit tests (encodings, line endings, find / replace, paths, the assembly routines) and about 450 gui checks that drive the real exe on a private desktop (nothing shows on your screen) all pass: `tools\verify.bat ui`.

## status

every menu item works: file (new, new window, open, recent, save, save as, page setup, print, exit), edit (undo ... select all, time / date, find, find next / previous,
replace, go to, clear line), format (word wrap, font, tab size, line ending, encoding), view (zoom, status bar, theme), help (the about box links to yuru.be). find / replace, go to, font, encodings, about and help
are our own dark / light dialogs; open, save as, print and page setup are the native windows dialogs (they follow windows' own dark / light mode, not the app theme). the encoding and line ending of a file are set from the
format menu or the status bar, not in the save as dialog.

how each part was verified, and what is still open, is in `NOTES.md` (status) and `TODO.md`.

## build

needs visual studio's c++ build tools (x86 target) and the windows sdk (python 3 for the layout check).

    build.bat                 release build -> build\notepad-mint.exe (zero warnings at /W3)
    build.bat dbg             debug build (symbols, build\dbg.log tracing)

`build.bat` looks for `vcvarsamd64_x86.bat` of visual studio 18 community and falls back to `vswhere`.

## installer and release

    tools\installer.bat [version]    build\notepad-mint-<version>.exe (standalone) + build\notepad-mint-<version>-setup.exe (NSIS installer)

needs [NSIS 3](https://nsis.sourceforge.io/) (`makensis` on the path, in program files, or the portable zip unpacked to `build\nsis-3.10`); the installer script is `installer\notepad-mint.nsi`.

the release pipeline is `.github/workflows/release.yml`: push a tag `v1.2.3` (keep the version in step with `FILEVERSION` in `src/notepad_mint.rc`) and github builds the exe, checks its imports, runs the unit tests, builds the installer and publishes a release with both files.

    git tag v1.0.0 && git push origin v1.0.0

## checks

    tools\verify.bat          release build + warning scan, imports, layout check, unit tests, smoke test
    tools\verify.bat ui       ... plus the message-driven gui tests (tests\ui\ui_test.ps1; private desktop, nothing shows)
    tools\verify.bat frame    ... plus the title strip test (tools\frame_test.ps1: real mouse input, it moves the mouse for a few seconds)

github ci for every push is switched off (the maintainer builds and tests locally with `tools\verify.bat ui`): the workflow with the same steps (minus the gui and title strip tests) is parked as `.github/workflows/build.yml.disabled`, rename it to `build.yml` to turn it on. only the release workflow runs, on version tags.
`tools\` also has `pw_shot.ps1` (screenshot on a private desktop: nothing shows; `-File`, `-Theme`, `-Sel`, `-HScroll`, `-VScroll`), `shot.ps1` (launch, send keys / a menu command / a mouse drag, screenshot on the real desktop), `cc.bat` (compile-check one file), `probe.bat` (experimental build), `flicker_test.ps1` (counts flicker frames while the window is resized: it shows the exe on your real desktop for a few seconds, takes no focus and sends no input) and `make_icon.ps1` (builds the icon).

## notes

the app icon is the real notepad icon recolored mint by `tools/make_icon.ps1` (it reads it from the local `notepad.exe`),
so it is derived from microsoft's artwork. there is no license file yet.
