# TODO - notepad mint

state (2026-10-08): 1.0.8 restores the saved caret view before keyboard navigation after manual scrolling, preserving selection anchors and native undo. the supported feature set is covered by `tools\verify.bat ui`. `NOTES.md` ("status") says per item how it was verified
and what was never verified; this file is what is still open, plus the rules for whoever changes the code next. the original per-module specs of the dialogs live in git history (`TODO.md` at 78e8b0a).

## 0. hard rules (breaking any of these breaks the build or the product)

- **raw c17 + x86 masm, 32-bit only.** no crt, no `windows.h`, no third-party code. the exe links with `/NODEFAULTLIB`, so any crt call
  (`strlen`, `wcslen`, `malloc`, `printf`, `qsort`, `swprintf` ...) is an *unresolved external at link time*. `memset/memcpy/memmove/memcmp` come from `src/rt.asm`.
  use the helpers in `src/util.c` (`mem_alloc`, `wcopy`, `wcat`, `wcmp`, `wcmpi`, `wlow`, `wtoi`, `PathName/PathDir/PathJoin`, `DefaultDocName` ...) and win32 (`wsprintfW`, `lstrlenW` ...).
  **no 64-bit integer division / modulus / shifts and no floating point** (they need crt helpers such as `__alldiv`); use `MulDiv` for scaled math.
- every win32 function, struct and constant you use must be declared in `src/w32.h` with the exact **32-bit sdk** signature / layout (stdcall, `API` = dllimport). `tools\layout_check` proves it against the real sdk:
  run it after every `w32.h` edit. only kernel32 / user32 / gdi32 may be imported statically; anything else (dwmapi, uxtheme, shell32, comdlg32 ...) is loaded with `LoadLibraryW` + `GetProcAddress`
  (see `UiInit` in `ui.c`). win10-only apis must be looked up at run time too (see `UiDpiForWindow`).
- **all user-visible text is lowercase** (labels, buttons, titles, messages).
- dpi: coordinates you hand to the ui helpers (`UiLabel/UiEdit/UiButton/DlgFrame/DlgOpen`) are 96-dpi pixels; any raw pixel math goes through `S()`.
- fonts: dialogs use `g_fontUI` (segoe ui 9pt, `DEFAULT_CHARSET`), the main window chrome (menu bar, status bar, title strip) uses `g_fontMenu` = the editor font face at a static `CHROME_PX` (11) px (`g_fontMenuB`, its bold twin, draws the title's name).
  **never use tahoma / microsoft sans serif for ui text**: on a japanese system locale (the dev machine is ja-JP) they draw `\` as a yen sign in every charset (measured, see `tools/fonttest.c`).
- the palette `C_*` is a set of **runtime values** (`g_pal`, dark / light): never use them in static initialisers, `case` labels or constant expressions, and never cache a brush made from them across a theme switch.
- a custom window class that handles `WM_NCCREATE` must still call `DefWindowProcW` for it (that is what stores the window text, otherwise buttons are blank).
- keep `build.bat` producing `build\notepad-mint.exe` with **zero warnings at /W3**; keep the exe's imports to kernel32 / user32 / gdi32 only.
- dialogs follow the `MsgDlg` pattern in `dialog.c`: a struct whose first member is `DlgBase`, one window class per dialog registered once with `RegClass`, a wndproc that starts with
  `DlgBase *b = DlgFromHwnd(h, m, l); if (!b) return DefWindowProcW(...)` and ends with `if (DlgCommon(b, m, w, l, &r)) return r;`. open with `DlgOpen(b, cls, title, cw, ch, modeless)`,
  run modal ones with `DlgRunModal(b)` (it disables every other live window of ours and re-enables them). default button id = `IDOK`, esc = `IDCANCEL`. native `COMBOBOX` / trackbar / comctl32 controls cannot be darkened: do not use them.
  native `LISTBOX` is fine but create it owner-draw (`LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | LBS_NOTIFY`), draw the selection with `C_ACCENT` / `C_FACE`, call `DarkScroll()` on it.
  dropdown-like choices = an `mp_btn` that pops `MenuPopup()` (ids >= 1000 so they never collide with `IDM_*`).
- the scrollbar auto-hide logic in `src/edit.c` (`UpdateBars` / `EditScrollSoon`) was done with a real display and verified with screenshots: don't regress it.

## 1. the feedback loop

- **locally**: `tools\verify.bat` (build + warning scan, imports, layout check, unit tests, smoke test), `tools\verify.bat ui` adds the message-driven gui suite (`tests\ui\ui_test.ps1`, private desktop, nothing shows),
  `tools\verify.bat frame` adds the title strip test with real mouse input. `tools\pw_shot.ps1` takes screenshots on the private desktop (nothing shows, safe while you work), `tools\shot.ps1` on the real desktop (`-Keys`, `-Cmd <IDM_ id>`, `-Drag`), `tools\cc.bat file.c` compile-checks one file,
  `tools\probe.bat <cl switches>` builds an experimental copy into `build\probe` without touching the real build.
- **ci is switched off** (the maintainer builds and tests locally; only the cloud agent needs ci): the workflow is parked as `.github/workflows/build.yml.disabled` (github ignores it). it does, on windows-latest with msvc x86:
  build with a warning scan, import check, layout check, unit tests, smoke test with a screenshot artifact, debug build with a warning scan. the cloud agent can turn it on by renaming it back to `build.yml`.
- **linux-side syntax check** (optional, fast): `tools/linux_check.sh`.

## 2. open items (none blocks the build)

0. **release pipeline** (`.github/workflows/release.yml`, tag `v*`): releases 1.0.0 through 1.0.3 ran successfully on windows-latest. version 1.0.4 verifies the exact release tag, warnings, executable size and sdk layouts as well as tests and imports. no license decision yet (the icon is derived from microsoft's notepad icon).
1. **the parked ci workflow never ran.** every script it calls was run locally, but its yaml is unproven (and the smoke step is `continue-on-error`): if it is ever turned on, expect to fix it on the first windows-latest run.
2. **big files are slow to open** because of the stock edit control, not our code: setting the text makes the control build its line index and measure every line (about 44 us per line, ~0.65 s per mb: 20 mb = 13 s,
   50 mb = 31 s, the window shows "not responding" meanwhile). profiled: `DocRead` takes 62 ms for 20 mb, the bar logic ~0 ms, and `WM_SETTEXT` and `EM_SETHANDLE` cost the same.
   fixes would be a chunked loader that pumps messages and shows progress, or our own text view instead of the native edit.
3. **printing was never run against a printer or pdf driver.** the wrap logic is tested, page setup opens; the job runs inside the command handler (no abort dialog, no message pump).
4. dialogs are not re-laid-out when dragged to a monitor with another dpi (the main window is).
5. ime composition in the edit is untested (no ime available to drive it here).
6. the custom radio buttons are both tab stops (native: only the checked one); arrow keys already move and select.
7. windows 11 snap layouts flyout on the maximize button is not available: the strip's buttons are `HTCLIENT` (own mouse handling) so they can't answer `HTMAXBUTTON`.
9. `tools\layout_check` checks argument byte counts of the api prototypes, not their argument types.
10. no license decision yet (the icon derives from microsoft's notepad icon, see `README.md`).
11. **the light face `#dfdee1` is a guess** (it was `#e4e1da`; on request 5 less red, 3 less green, 7 more blue). the maintainer said the light app background was too dark and had to match the status bar; title strip, menu bar, status bar and dialogs already shared one colour, so that colour was lightened.
    if another region was meant, say which (the palette is the `g_themes[1]` row in `ui.c`).
12. native open / save as dialogs follow windows' own app mode, not the app theme (dark on this dark-mode windows in both of our themes: a dark dialog over the light theme), and have no encoding / line ending picker: both come from the format menu / status bar.
13. selected text is windows' default highlight (an accent overpaint flickered and was removed); single-line edits in dialogs have it too. the editor adds a small block for each selected line break (`edit.c`, "selected line breaks"; the dialogs' single-line edits have none: no line breaks there);
    a left to right line in the right to left editor ends at the margin, so its selected break has no room for the block (empty lines and right to left text get it).
14. the stock edit's undo is single level (a second ctrl+z redoes). "undo back to the original = not modified" therefore holds for the last edit only; type-and-delete works for any length.
15. the scrollbars are our classic bars over the native ones (`sbar.c`): the native bar keeps working underneath and is only hidden by `WS_CLIPSIBLINGS` + the overlay on top, so a new control with a scrollbar must go through `DarkScroll()`.
    the overlays poll their target every 40 ms (cheap; the editor also syncs on every scrolling message). the stock edit's horizontal range is capped by the control itself (about 9200 px: a longer line can't be scrolled to its end, with the native bar either).
16. the maintainer reported the horizontal scrollbar logic "seems inverted": **not reproduced** (empty, wide only, tall only, tall + wide, wrap on / off all end in the right state, `T15`). needs the exact steps.
17. the about link was never click-tested (it opens the default browser); `T17` checks the control, its text and its place.
18. a drop inserts the dropped paths at the caret; **shift+drop opens the files** (the behaviour before 2026-10-06). the plain drop is tested (`T24`, an HDROP posted by the test); the shift variant reads the real key state, which messages cannot set: it runs the old code unchanged but was not run.
    a real drag from Explorer was not tried either (no way to drive one on a private desktop): the `WM_DROPFILES` the shell posts is what `T24` posts.
19. **the row cut off at the bottom of the editor is drawn by us** (`edit.c` "partly visible rows"; the stock control draws whole rows only). left blank, as the whole band used to be: a right to left editor's band, rows with arabic / hebrew presentation forms, rows with right to left letters (or bidi marks) while any of the row is selected, rows wider than 16 bits of text width.
    `EDIT_BAND_INSET` (`mp.h`) is 0: the cut-off row runs to the editor's bottom edge. if it should stop above it (a gap, the padding stays blank) set it to a number of 96-dpi pixels (`EDIT_PAD` = the old padding). **not done**: rendering only what is visible of a huge file (virtualised text, an own text view) - that is item 2.
20. **editor text disappearing during resize: complete-frame buffering in 1.0.6.** 1.0.5 deferred standalone erases, but native painting could still clear live text. the maintainer identified multilingual documents as the trigger. T38 observes the real in-paint erase in plain and multilingual controls, both themes and wrap states; it also checks partial repaint clipping and GDI resource lifetime. see `NOTES.md` for evidence and sampling limits.
21. **theme button (menu bar, right end) and the selection in the status bar: three readings to confirm.** (a) the button's icon is the theme IN USE (moon in the dark theme, sun in the light one: "it just switches between the icons"); if it should show the theme a click leads to, flip the condition in `BarPaint` (`menu.c`). the icon is 20% opaque at rest (`BAR_BTN_OPACITY`) and 12 px (`BAR_BTN_ICON`), both asked for in words ("opacity .2", "a little bit smaller": 14 -> 12 is my reading of the size). (b) the status bar's `5 L` for a selection counts the lines it covers, and a selection that ends right at the start of a line does not count that line (3 whole lines = 3 L, not 4); `54 B` is what a save would write for the selection in the document's encoding and line ending, without a bom. (c) the longer `162:54 [5 L 54 B]` can widen a window that is close to its minimum width while something is selected (the no-panel-is-cut-off rule); say if it should clip instead. the buttons have a tooltip (our own small popup, `mp_tip`: the system tooltip control needs comctl32 = a 4th dll) saying what the button does and its key combo, and take no keyboard focus: alt+x and view > theme are the keyboard ways.

## 3. definition of done

- [x] `build.bat` clean (zero /W3 warnings), imports only kernel32 / user32 / gdi32, `src/stubs.c` gone
- [x] every menu item does what notepad's does (plus the extras in `README.md`): driven by `tests\ui`, see `NOTES.md`
- [x] `NOTES.md` / `README.md` updated
- [x] ci: switched off on purpose (only the cloud agent needs it); the local equivalent is `tools\verify.bat ui`, all green
- [x] release changes and validation recorded in `RELEASE_NOTES.md` and the 1.0.4 audit in `NOTES.md`
