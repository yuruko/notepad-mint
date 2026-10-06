# notepad mint

free replacement for notepad.exe: dark, mint accent, no ai / sign-in / telemetry. plain text only.
written in raw c + **32-bit x86** masm. no crt, no libs, no windows.h (every win32 decl is in `src/w32.h`).
exe imports only kernel32/user32/gdi32; dwmapi/uxtheme (dark frame, dark scrollbars), shell32 (drag & drop) and
comdlg32 (page setup / print only) are loaded at run time. one 32-bit exe runs on 32-bit windows, 64-bit windows and arm.

## user requirements (verbatim intent)
- name everywhere (title, about, version info, exe, appdata dir): **"notepad mint"**, lowercase aesthetic. exe = `build\notepad mint.exe`
- function EXACTLY like notepad.exe (same menus/layout/status bar/dialog wording) + nice extras + custom theming
- colors: accent `#9df5bd`, app/editor bg default `#161418`, text default `#ffffff`; user can change text + bg colors
- default font consolas 13pt (fallback lucida console / courier new); better font+color settings ui than the stock dialog
- windows-2000-esque as far as possible: native frame, classic bevels, flat menu bar, sunken fields; dark (default) or light. **no size grip** on the status bar (removed on request): the window resizes by its frame
- **no accent-colored window border** (frame border is a neutral dark gray)
- **menu bar / popups / status bar use the editor font, 3pt smaller, never above 14pt** (`UiSetChromeFont`, `CHROME_PT_LESS`, `g_fontMenu`; the status bar and the menu bar share that one font object).
  (the native title bar text is drawn by windows in the system caption font: it can't follow the editor font without a custom-drawn title bar)
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
src/mp.h       shared decls, palette (C_*), command ids (IDM_*), Prefs, DocState
src/util.c     mem/string/path helpers (+ Dbg() file logger in dbg builds)
src/ui.c       bevel drawing, dwm dark frame, custom button class `mp_btn`, dialog scaffolding (DlgBase), MpAsk, fonts, dynamic dpi lookups
src/menu.c     custom menu bar + popup engine with submenus (no native menus)
src/status.c   custom status bar (clickable size/eol/enc panels, widths measured from the chrome font; no size grip)
src/doc.c      DocRead/DocWrite: encoding detect, code page table, crlf normalising
src/edit.c     native EDIT wrapper: create/recreate (word wrap), subclass, font+size ladder, logical line/col, word delete, goto, rtl
src/main.c     entry `start`, main window, commands, file flow, settings, accelerators, command line
src/about.c    about box + help topics
src/find.c, filedlg.c, fontdlg.c, print.c   find/replace+goto, open/save/encoding dialogs, font&colors, print/page setup
src/notepad_mint.rc + .manifest   icon id 1, versioninfo, manifest (per-monitor-v2 dpi, supportedOS, asInvoker)
build.bat      `build.bat` from the project root (x64-hosted x86 cl/ml/link via vcvarsamd64_x86; from git bash: `cmd.exe /c "<abs path>\build.bat"`).
               `dbg` arg = symbols + build\dbg.log tracing.
               `SRCS=...` env var overrides the c file list (interim builds)
tools/         shot.ps1 (launch+keys+screenshot), crop.ps1, px.ps1, cc.bat (compile-check one file), cc_asm.bat, fonttest.*, layout_check/
```

## decisions / design notes
- edit control = native EDIT (multiline, ES_NOHIDESEL). text kept utf-16 + CRLF inside; DocRead normalises, DocWrite converts.
  word wrap toggle = recreate the edit control (like notepad). colors via WM_CTLCOLOREDIT. selection color stays system blue (unavoidable with native edit).
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
  ctrl+(=,+,numpad+) bigger, ctrl+(-,numpad-) smaller, ctrl+(0,numpad0) reset. only translated for messages to the main window / its children.
- file dialogs: custom dark dialogs (path edit, up, listbox, file name, type + encoding + line ending dropdowns made of mp_btn + MenuPopup). default ext .txt rule like notepad.
- prefs: `%APPDATA%\notepad mint\settings.ini` (utf-16 ini so any font name works). window placement saved. new windows cascade.
- dpi: manifest per-monitor-v2; everything scaled through `S()`; WM_DPICHANGED rebuilds fonts. win10-only dpi apis are looked up at run time.
- accent usage: menu/popup highlight = mint fill + dark text, hot text = mint, default button frame = mint. NOT the window border.
- fonts: dialogs use segoe ui 9pt, DEFAULT_CHARSET. **measured on this ja-JP machine: tahoma + microsoft sans serif draw '\' as a yen sign
  in every charset, segoe ui/arial do it with ANSI_CHARSET** (tools\fonttest.c). consolas/verdana/courier new/lucida console are fine.
- system locale of the dev/user machine is ja-JP (ansi code page 932): "ansi" in this app = shift-jis there.
- notepad parity details: title `*name - notepad mint`; unsaved prompt "do you want to save changes to <name>?" save/don't save/cancel;
  cmdline path that doesn't exist => "cannot find the <path> file. do you want to create a new file?"; time/date = short time (no secs) + space + short date;
  not-found msg `cannot find "<text>"`; goto line error "the line number is beyond the total number of lines"; default ext txt on save.

## status (snapshot when the repo was first pushed)
done + screenshot-tested: main window, custom dark menu bar/popups/status bar (chrome font), word wrap, font-size zoom (ctrl+plus/minus/0/wheel, 10..96pt),
  neutral window border, about box, help topics, 32-bit build (pe32, large-address-aware), command line / drag & drop open, in-place save (ctrl+s) of a file that has a path,
  new / new window / exit prompts, settings load/save, utf-8/utf-16/code page read+write code (doc.c; not yet round-trip tested).
placeholders in src/stubs.c (each shows "isn't written yet"): find, replace, go to, open dialog, save as dialog, encoding pickers (other code page / reopen with),
  font & colors dialog, print, page setup.  => the specs for them are in git history / were: find.c (find+replace modeless + goto), filedlg.c (custom dark open/save with
  encoding + line ending dropdowns, EncDlg), fontdlg.c (font list, size 10..96, rgb sliders + hex + presets, live preview, reset), print.c (comdlg32 on demand, text pagination).
  (a first attempt to have helpers write them was interrupted before any file landed.)
experimental, OFF: custom title strip (src/frame.c, `FRAME_CUSTOM` 0) so the title could use the chrome font - windows still drew its own caption above it
  (WM_NCCALCSIZE override had no effect; FrameNcCalc never logged). the native dark title bar is used meanwhile.
todo: the dialogs above, tools/layout_check (compare w32.h struct layouts + constants with the real sdk), functional tests (encoding/eol round trips, wrap, find/replace, dpi),
  a final polish pass, license decision.

## tooling quirks
- RunBash: don't put powershell code in a bash heredoc (gets routed to powershell and breaks). use the Write tool for files.
- `cmd.exe /c build.bat` needs the absolute path. screenshots: tools\shot.ps1 (CopyFromScreen of the dwm frame bounds; forces the window to the foreground
  with AttachThreadInput; the keys go to the real foreground window, so runs are flaky if something steals focus).
- the test harness steals keyboard focus: the user's own typing can land in the test window.
