# notepad mint

free replacement for notepad.exe: dark (default) or light, mint / dark green accent, no ai / sign-in / telemetry. plain text only.
written in raw c + **32-bit x86** masm. no crt, no libs, no windows.h (every win32 decl is in `src/w32.h`).
exe imports only kernel32/user32/gdi32; dwmapi/uxtheme (dark frame, scrollbar themes), shell32 (drag & drop, the about link) and
comdlg32 (open / save as, page setup, print) are loaded at run time. one 32-bit exe runs on 32-bit windows, 64-bit windows and arm.

## user requirements (verbatim intent)
- name everywhere (title, about, version info, exe, appdata dir): **"notepad mint"**, lowercase aesthetic. exe = `build\notepad mint.exe`
- function EXACTLY like notepad.exe (same menus/layout/status bar/dialog wording) + nice extras + custom theming
- colors: **two themes only, dark (default) and light**, switched from view > theme and saved as `[view] theme=dark|light`. dark: mint accent `#9df5bd`, chrome `#161418`, **black** editor.
  light: a **dark green accent `#0a552d`** (the title strip's green, a little darker; white text on it), one light face `#e4e1da` shared by the title strip, menu bar, status bar and dialogs, **white** editor.
  (the face value is a guess at "the app background must match the status bar": every chrome part already used one colour, it was just too dark.)
  custom text / background colours were removed (maintainer's decision): `g_pf.fg / bg` come from the theme and are never saved
- **hover = accent as the BACKGROUND** (with on-accent text), never the accent as text colour: menu bar items, popup items, status bar panels, push buttons, check / radio labels, the about link, the title strip's buttons.
  **selected text and the caret use the accent too.** **one border only** between the menu bar and the editor and between the editor and the status bar (the editor's 2px sunken frame; the bars draw no line of their own)
- default font consolas 13pt (fallback lucida console / courier new); the font dialog is **font only** (face, size 10..96, bold, italic, preview, reset)
- windows-2000-esque as far as possible: native frame, classic bevels, flat menu bar, sunken fields; dark (default) or light. **no size grip** on the status bar (removed on request): the window resizes by its frame
- **no accent-colored window border** (frame border is a neutral dark gray)
- **menu bar / popups / status bar use the editor font, 3pt smaller, never above 14pt** (`UiSetChromeFont`, `CHROME_PT_LESS`, `g_fontMenu`; the status bar and the menu bar share that one font object).
  the **title bar follows it too**: the caption is drawn by us in the client area (`src/frame.c`, `FRAME_CUSTOM` 1), title text in the accent colour while the window is active. (a native caption is drawn by windows
  in the system caption font and can't follow the editor font; build with `/DFRAME_CUSTOM=0` via `tools\probe.bat` to get it back)
- status bar like notepad: `ln, col | font size | line ending | encoding`; line ending + encoding selectable
- handle ALL languages: utf-8/utf-16 (+bom), ansi, legacy code pages (reopen/save as), ime, rtl toggle, unicode control chars
- **ctrl+plus / ctrl+minus / ctrl+wheel change the font SIZE** (not a percentage): steps along a ladder 10..96pt
  (1pt steps up to 20, then 2, 4, 6, 8). **ctrl+0 returns to the size picked in the font dialog** (default 13). font size range 10..96pt everywhere.
  `g_pf.pt` = chosen size (saved), `g_pf.cur` = working size (not saved; starts = pt). status bar panel shows `NN pt`.
- **32-bit only** build (one install for every machine)
- icon = notepad.exe icon recolored mint (`assets/notepad_mint.ico`, built by `tools/make_icon.ps1`)

## layout
```
src/w32.h      hand-written win32 types/consts/prototypes (32-bit x86 layouts; PRINTDLGW packed 1)
src/rt.asm     x86 masm: _memset/_memcpy/_memmove/_memcmp, __chkstk (esp must drop by exactly eax!), _mp_count_lf (sse2, no popcnt)
src/mp.h       shared decls, palette (C_* = runtime values of `g_pal`, never use them in static initialisers), command ids (IDM_*), Prefs, DocState
src/util.c     mem/string/path helpers (+ Dbg() file logger in dbg builds)
src/ui.c       bevel drawing, dwm dark frame, custom button class `mp_btn`, dialog scaffolding (DlgBase), MpAsk, fonts, dynamic dpi lookups
src/menu.c     custom menu bar + popup engine with submenus (no native menus)
src/status.c   custom status bar (clickable size/eol/enc panels, widths measured from the chrome font; no size grip)
src/doc.c      DocRead/DocWrite: encoding detect, code page table, crlf normalising
src/edit.c     native EDIT wrapper: create/recreate (word wrap), subclass, font+size ladder, logical line/col, word delete, goto, rtl
src/main.c     entry `start`, main window, commands, file flow, settings, accelerators, command line
src/about.c    about box + help topics
src/frame.c    the custom title strip (icon, title in the chrome font, min / max / close, hit testing, window menu)
src/find.c (+ search.c: the pure, unit-tested search / replace-all code), filedlg.c, fontdlg.c, print.c   find/replace + go to, the native open / save as dialogs (comdlg32) + our encoding picker, font dialog, print / page setup
src/notepad_mint.rc + .manifest   icon id 1, versioninfo, manifest (per-monitor-v2 dpi, supportedOS, asInvoker)
build.bat      `build.bat` from the project root (x64-hosted x86 cl/ml/link via vcvarsamd64_x86; from git bash: `cmd.exe /c "<abs path>\build.bat"`).
               `dbg` arg = symbols + build\dbg.log tracing.
               `SRCS=...` env var overrides the c file list (interim builds)
tools/         shot.ps1 (launch + keys / -Cmd + screenshot), crop.ps1, px.ps1, cc.bat (compile-check one file), cc_asm.bat, fonttest.*,
               probe.bat (experimental build into build\probe, e.g. `/DFRAME_CUSTOM=0`), frame_test.ps1 (title strip: real mouse input), smoke.ps1 (smoke test, used by verify.bat and the parked ci workflow),
               check_imports.ps1 (kernel32 / user32 / gdi32 only), layout_check/ (w32.h vs the real sdk), linux_check.sh (syntax check on linux)
tests/         unit/ (no-crt host tests of doc.c / search.c / util.c / rt.asm, `tests\unit\build.bat`), ui/ (message-driven gui tests), sample files
```

## decisions / design notes
- edit control = native EDIT (multiline, ES_NOHIDESEL). text kept utf-16 + CRLF inside; DocRead normalises, DocWrite converts.
  word wrap toggle = recreate the edit control (like notepad). colors via WM_CTLCOLOREDIT.
- **accent selection** (`edit.c` `SelPaint` / `PaintSelection` / `RepaintSel`): the stock control can only draw the system selection colour, so while a selection exists `WM_PAINT` is answered by us:
  the control paints itself into an off-screen bitmap (`WM_PRINTCLIENT`), then every selected run is overpainted in the accent (fill + on-accent text, positions from `EM_POSFROMCHAR`, tabs and partial lines included),
  and the bitmap is blitted once (no flicker). the stock highlight is one pixel wider than its text, so a run's fill is extended by one pixel when the pixel next to it in the captured paint still has the system highlight colour
  (`GetPixel` / `GetSysColor(COLOR_HIGHLIGHT)`): without that a 1px blue line stayed at the end of every run (found in a screenshot, checked by counting `#0078d7` pixels: 0 left in light / dark, wrap on / off, horizontal scroll, tabs, mouse drag).
  the stock control paints a *changed* selection directly (it never goes through our `WM_PAINT`), so `RepaintSel` forces a repaint after every message that can change the selection.
  **limit:** a line that contains right-to-left text keeps the stock blue (its runs are not contiguous on screen), and so does the whole editor while `WS_EX_RTLREADING` is on. single-line edits in dialogs keep the stock colour too.
- **accent caret** (`AccentCaret`): a bitmap caret whose bits are `(editor background xor accent)`, because the caret is drawn with xor: on the editor background it comes out exactly as the accent (measured `#0a552d` in light).
  re-made on `WM_SETFOCUS`, `WM_SETFONT` and when the theme changes (`EditApplyColors`).
- **dirty state by content** (`main.c` `g_clean`, `CleanMark`, `TextChanged`, `AppIsDirty`): the stock modified flag is sticky, so typing and deleting again, or undoing back to the original, would still ask to save.
  the clean state (what was loaded / saved, or blank for a new document) is kept as length + two 32-bit hashes (no 64-bit math, nothing is copied for a big file); a compare only happens when the control's flag is set and the length is back to the clean one.
  a new encoding / line ending is a change unless the document is empty and unsaved. `CleanMark` runs in `OnCreate`, `OpenDoc`, `WriteDoc`, `FileNew`. the title's `*` and the close prompt both use `AppIsDirty`.
- ctrl+a is an accelerator: in the find dialog it selects inside the focused text box, otherwise in the editor.
- scrollbars only when needed (edit.c `UpdateBars`): a native multiline edit always shows its bars (greyed). we show/hide them with `ShowScrollBar` after text / size / font changes
  (`EditScrollSoon` posts `WM_BARS` so the control finishes its own layout first). pitfalls found the hard way: (1) a hidden bar (WS_xSCROLL cleared) is no longer maintained by the control,
  so its range can't be read -> vertical need = `EM_GETLINECOUNT` vs lines that fit (`EM_GETRECT` / line height); horizontal need = show the bar with redraw off, read `GetScrollInfo`, hide again if unneeded;
  (2) that show/hide resizes the edit -> `WM_SIZE` -> re-schedule = endless posted-message loop that starves `WM_PAINT` (unpainted children) -> `g_inBars` guard; (3) reset scroll position when a bar goes away.
  verified: short doc = none, tall = vertical, wide = horizontal, both, word wrap on (vertical only), typing past the window, window enlarged (bar disappears).
- logical line/col (status bar, goto) = lf count over the edit's own buffer (EM_GETHANDLE + LocalLock), so they stay right with word wrap on.
  goto works with wrap on too (modern notepad behaviour, not the classic "grayed out").
- menus: bar = child window `mp_menubar`; popups = `mp_popup` (WS_EX_NOACTIVATE), private modal loop in `RunMenu` handles mouse/keys/mnemonics/submenus.
  alt+letter: the native path never reaches the main window when the edit has focus, so EditProc catches WM_SYSCHAR and forwards SC_KEYMENU.
  RunMenu drains leftover WM_CHAR after it ends (enter/space would otherwise be typed into the edit).
- enc model: `int enc`: 0..4 = ENC_UTF8/UTF8BOM/UTF16LE/UTF16BE/ANSI(system acp), >=100 = windows code page number. `DocRead(..., forceEnc)` (-1 detect).
  saving to an 8-bit code page flags lossy conversion (no best-fit): the user is asked first.
- rtl toggle = flip WS_EX_RTLREADING|WS_EX_RIGHT|WS_EX_LEFTSCROLLBAR on the edit; unicode control char table in main.c.
- accelerators: ctrl+n, ctrl+shift+n (new window = CreateProcess self), ctrl+o/s/shift+s/p, ctrl+f/h/g, f3/shift+f3, f5, f1,
  ctrl+(=,+,numpad+) bigger, ctrl+(-,numpad-) smaller, ctrl+(0,numpad0) reset. only translated for messages to the main window, its children and the modeless
  find dialog (so f3 / ctrl+g work from it, like notepad); modal dialogs run their own loop and never see them.
- dialogs: `DlgRunModal` disables every live window of ours (owner + the modeless find dialog) and re-enables exactly those. the dialog fonts (`g_fontUI`) are rebuilt only on a dpi change
  (the replaced pair is not deleted: an open find dialog may still hold it); a chrome font change only rebuilds `g_fontMenu`. dialog edits (`UiEdit`) are subclassed for ctrl+backspace (the stock
  edit inserts a DEL character). known gap: a dialog dragged to a monitor with another dpi is not re-laid-out.
- the font dialog lists only vector fonts (bitmap families like terminal / fixedsys are skipped) and, by default, only monospaced ones (probed by comparing the widths of "iiiiiiii" / "WWWWWWWW").
- printing (`print.c`) runs the whole job inside the command handler: no abort dialog, the window can show "not responding" for a very large document. the native print / page setup dialogs are windows' own (they follow its light / dark mode, not our theme).
- title strip (`frame.c`): the first attempt drew two title bars because the only `WM_NCCALCSIZE` sent by `CreateWindowExW` has `wParam` FALSE (the single-rect form) and nothing recalculated the frame until the first
  resize. fix: `FrameNcCalc` handles both forms, and `mp_main` forces one more recalculation with `SetWindowPos(SWP_FRAMECHANGED)` right after creating the window. the sizing border stays the system's
  (resize, aero snap, shadow), the strip answers HTCAPTION / HTTOP / HTTOPLEFT / HTTOPRIGHT, the three buttons are HTCLIENT (own mouse handling: no windows 11 snap-layout flyout on the maximize button).
  verified with `tools\frame_test.ps1` (real mouse input: hit tests, maximize / restore, double click, drag, win+left snap, minimize, close).
- file dialogs (maintainer's request: native ones): open / save as are `GetOpenFileNameW` / `GetSaveFileNameW` from comdlg32, loaded on first use (`PickFile` in `filedlg.c`), titles "open" / "save as", filter text documents / all files,
  default extension `.txt`, start folder = the current file's folder, else the last folder picked. a native dialog is not ours to theme: **it follows windows' own app mode, not the app theme** (measured on this machine: windows is
  in dark mode, and the dialog is dark in both our themes; with the light theme that is a dark dialog over a light app, and the other way round on a light system).
  there is **no encoding / line ending picker in them any more**: both come from the format menu and the status bar panels (`FileDlgSave(owner, path, cap)` takes no enc / eol). `EncDlg` (code page list) stays ours.
  while one is up the modeless find dialog is disabled too (replace all must not edit the document under it). if comdlg32 can't be loaded the app says so in a message box and does nothing.
- prefs: `%APPDATA%\notepad mint\settings.ini` (utf-16 ini so any font name works). window placement saved. new windows cascade.
- dpi: manifest per-monitor-v2; everything scaled through `S()`; WM_DPICHANGED rebuilds fonts. win10-only dpi apis are looked up at run time.
- accent usage: menu bar / popup items, status bar panels, push buttons and check / radio labels fill with the accent (text in `C_ON_ACCENT`) when hovered or keyboard-selected; the default button frame is the accent.
  light mode: accent = accentFg = `#0a552d` (text on the light face and lines use the same green, onAccent is white). NOT the window border.
- scrollbars: dark keeps `SetWindowTheme(h, L"DarkMode_Explorer", NULL)` (thin dark bars, arrows only on hover); light uses `SetWindowTheme(h, L"", L"")` = the **classic scrollbar with always visible arrow buttons**
  (the explorer-themed light bar has no arrows at all; the maintainer likes the classic one: keep). both in `DarkScroll` (ui.c), re-applied on a theme switch.
- dialogs and message boxes carry the app icon (`DlgOpen` sends `WM_SETICON` big + small from resource 1). the about box has a link, "yuru.be" (`about.c`, class `mp_link`, `ShellExecuteW` found with `GetProcAddress`)
  that opens https://yuru.be (hover = accent background like every other control).
- fonts: dialogs use segoe ui 9pt, DEFAULT_CHARSET. **measured on this ja-JP machine: tahoma + microsoft sans serif draw '\' as a yen sign
  in every charset, segoe ui/arial do it with ANSI_CHARSET** (tools\fonttest.c). consolas/verdana/courier new/lucida console are fine.
- system locale of the dev/user machine is ja-JP (ansi code page 932): "ansi" in this app = shift-jis there.
- default document name (maintainer's request, replaces "untitled"): an unsaved document is called `mintXXXX`, XXXX = 4 characters of 0-9 a-z (base 36, zero padded) of the sum
  year + month*100 + day + seconds since midnight, local time (`DefaultDocName` in util.c, unit tested; e.g. 2026-10-05 21:53:42 -> 2026 + 1005 + 78822 = 81853 -> `mint1r5p`).
  generated at startup and again on file > new (`g_doc.name`, `NewDocName` / `AppDocName` in main.c); it is the title, the name in the "save changes to ..." prompt, the print job / header name, and
  what the save as dialog proposes in its file name box (".txt" is added). the sum stays below 100000 for any year up to 9999, so the first character is only ever 0, 1 or 2 (change the formula in one place).
- notepad parity details: title `*name - notepad mint`; unsaved prompt "do you want to save changes to <name>?" save/don't save/cancel;
  cmdline path that doesn't exist => "cannot find the <path> file. do you want to create a new file?"; time/date = short time (no secs) + space + short date;
  not-found msg `cannot find "<text>"`; goto line error "the line number is beyond the total number of lines"; default ext txt on save.

## status (2026-10-06)
every menu item is implemented (no placeholders left, `src/stubs.c` is gone). the release build has zero /W3 warnings and the exe imports only kernel32 / user32 / gdi32.
how each part was verified (everything below ran locally; `tools\verify.bat ui` repeats all of it except the title strip and the screenshots):
| what | how | result |
|---|---|---|
| build | `build.bat`, log scanned for warnings | clean, 187 kb |
| imports | `tools\check_imports.ps1` (reads the pe import table) | kernel32 / user32 / gdi32 only, 158 functions (comdlg32 / shell32 / uxtheme / dwmapi are loaded at run time) |
| `src/w32.h` against the real sdk | `tools\layout_check` (two translation units + a linker pass; fails on a broken copy: tested) | 34 structs, 263 fields, 448 constants, 140 macros, 54 typedefs, 192 api prototypes: 0 mismatches |
| doc / search / util logic | `tests\unit` (no crt, like the app) | 375 checks: encodings x line endings round trips, bom, utf-16 without bom, invalid utf-8, lossy writes, 1 mb round trips, find / replace-all, wildcard, paths, default name. 1 skipped (accented latin needs cp 1252, this box is cp 932) |
| dialogs and commands | `tests\ui\ui_test.ps1`: drives the real exe with window messages on a private desktop (nothing shows, no keystroke can leak) | 247 checks (T1-T13, T15-T17): startup decode, find (case, wrap, up / down, cannot find), replace / replace all (one undo step), go to, **open and save as through the native comdlg32 dialogs** (the `#32770` windows are driven by messages, no dialog text is asserted: open / cancel / missing file / dirty prompt, utf-8 / utf-16 / ansi round trips, `.txt` rule, overwrite prompt yes / no, encoding and line ending from the format menu, start folder), reopen with encoding, theme (settings + pixels), font dialog (clamps, presets, reset), about / help / page setup, big file, exit prompts, default name, **scrollbars only when needed (T15: empty / wide / tall / both / wrap on / off)**, **dirty state by content (T16: type + delete, undo back, eol change and back, close prompt only for a real change)**, **the about link (T17: control, text, place)**. 1 skipped (editor focus after a dialog needs a visible desktop; it passed in an earlier visible run). the test exe copy is named `mint_ui_test.exe` so the native dialogs' per-exe-name "last folder" registry entry of the real app is not touched |
| title strip | `tools\frame_test.ps1`: real mouse input on the real desktop | 18 checks (hit tests, maximize / restore with the same size back, double click, drag, win+left snap, window menu, minimize, close): all pass on the final exe. needs the desktop to itself: two runs made while someone was using the mouse / keyboard failed at random |
| looked at in screenshots | dark and light main window, find, replace, go to, native open / save as, font, about, menus, rtl, new-window cascade, 50 mb file | as intended |
| light palette, hover, single border, accent selection / caret, scrollbars (2026-10-06 batch) | `tools\shot.ps1` on the real desktop with real keys and a real mouse drag; the pixels were measured with python / PIL | light accent `#0a552d` and face `#e4e1da` as designed; hover = accent background on menu bar, popups, status panels, buttons, check / radio labels, about link; one border line under the menu / above the status bar; the caret pixel is exactly the accent on the editor background (a white inverse bar inside a selection); selection in the accent with partial lines, tabs, word wrap on, horizontal scroll and a mouse drag (held and released): **0 stock-blue `#0078d7` pixels left** after the 1 px fix; runtime theme switch dark -> light -> dark re-themes the scrollbars (classic with arrows in light, thin in dark) and the selection / caret; dialogs carry the app icon |
| ctrl+backspace / ctrl+delete, rtl toggle, new window cascade, settings.ini round trip | by hand with `tools\shot.ps1` | work |
| print | the page wrap logic was property tested against a reference model (6.9 m cases); page setup opens and closes (gui test) | **no job was ever sent to a printer or pdf driver**: unverified |
| about link | `T17` checks the control (class, text, place); the hover look was seen in a screenshot | **the click was never tried** (it opens the default browser): unverified |
| ci | switched off on purpose (only the cloud agent needs it): the workflow is parked as `.github/workflows/build.yml.disabled`; the scripts it calls were run locally | the workflow itself never ran on github |
five read-only reviews of find / filedlg / fontdlg / print / the theme code found about 30 candidate defects. each was re-checked before touching code: two were refuted by measurement (listbox type-ahead
needs no `WM_CHARTOITEM` handler, and Enter on a focused non-default button does not press ok), the real ones were fixed (see git log). known limits are in `TODO.md`.

## tooling quirks
- RunBash: don't put powershell code in a bash heredoc (gets routed to powershell and breaks). use the Write tool for files.
- `cmd.exe /c build.bat` needs the absolute path. screenshots: tools\shot.ps1 (CopyFromScreen of the dwm frame bounds; forces the window to the foreground
  with AttachThreadInput; the keys go to the real foreground window, so runs are flaky if something steals focus).
- `tools\shot.ps1` and `tools\frame_test.ps1` steal the foreground (and the mouse): the user's own typing can land in the test window (it happened: stray text showed up in the editor).
  `tests\ui\ui_test.ps1` does not: it runs the app on a private desktop (`CreateDesktop`) and uses window messages only. `shot.ps1 -Cmd <IDM_ id>` posts a menu command by id; `-Mouse "x,y"` parks the real mouse (hover states),
  `-Burst N` takes N captures in a row (blinking caret), `-Drag "x1,y1,x2,y2" [-DragHold]` drags the real mouse (selection painting). `shot.ps1` kills every running copy of `build\notepad mint.exe` at the start
  and the end (so close your own notepad mint first); the gui tests run temp copies of the exe, those are left alone (an earlier version killed every "notepad mint" process, the tests' copies included).
- batch files: the current directory is not on the command lookup path here, so every `cmd /c` / `call` uses an absolute path (`tools\verify.bat` builds one from `%~dp0`);
  `a && b & c` runs `c` unconditionally (use parentheses).
- the unit tests, layout_check, probe builds and the gui suite all write under `build\` (git-ignored) and never touch `build\notepad mint.exe` except `build.bat` itself.
