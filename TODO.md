# TODO - notepad mint

state (2026-10-05): feature complete. every menu item works, the build is clean and the checks in `tools\verify.bat ui` pass. `NOTES.md` ("status") says per item how it was verified
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
- fonts: dialogs use `g_fontUI` (segoe ui 9pt, `DEFAULT_CHARSET`), the main window chrome (menu bar, status bar, title strip) uses `g_fontMenu` = the editor font minus `CHROME_PT_LESS` (3) pt.
  **never use tahoma / microsoft sans serif for ui text**: on a japanese system locale (the dev machine is ja-JP) they draw `\` as a yen sign in every charset (measured, see `tools/fonttest.c`).
- the palette `C_*` is a set of **runtime values** (`g_pal`, dark / light): never use them in static initialisers, `case` labels or constant expressions, and never cache a brush made from them across a theme switch.
- a custom window class that handles `WM_NCCREATE` must still call `DefWindowProcW` for it (that is what stores the window text, otherwise buttons are blank).
- keep `build.bat` producing `build\notepad mint.exe` with **zero warnings at /W3**; keep the exe's imports to kernel32 / user32 / gdi32 only.
- dialogs follow the `MsgDlg` pattern in `ui.c`: a struct whose first member is `DlgBase`, one window class per dialog registered once with `RegClass`, a wndproc that starts with
  `DlgBase *b = DlgFromHwnd(h, m, l); if (!b) return DefWindowProcW(...)` and ends with `if (DlgCommon(b, m, w, l, &r)) return r;`. open with `DlgOpen(b, cls, title, cw, ch, modeless)`,
  run modal ones with `DlgRunModal(b)` (it disables every other live window of ours and re-enables them). default button id = `IDOK`, esc = `IDCANCEL`. native `COMBOBOX` / trackbar / comctl32 controls cannot be darkened: do not use them.
  native `LISTBOX` is fine but create it owner-draw (`LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | LBS_NOTIFY`), draw the selection with `C_ACCENT` / `C_FACE`, call `DarkScroll()` on it.
  dropdown-like choices = an `mp_btn` that pops `MenuPopup()` (ids >= 1000 so they never collide with `IDM_*`).
- the scrollbar auto-hide logic in `src/edit.c` (`UpdateBars` / `EditScrollSoon`) was done with a real display and verified with screenshots: don't regress it.

## 1. the feedback loop

- **locally**: `tools\verify.bat` (build + warning scan, imports, layout check, unit tests, smoke test), `tools\verify.bat ui` adds the message-driven gui suite (`tests\ui\ui_test.ps1`, private desktop, nothing shows),
  `tools\verify.bat frame` adds the title strip test with real mouse input. `tools\shot.ps1` takes screenshots (`-Keys` or `-Cmd <IDM_ id>`), `tools\cc.bat file.c` compile-checks one file,
  `tools\probe.bat <cl switches>` builds an experimental copy into `build\probe` without touching the real build.
- **ci is switched off** (the maintainer builds and tests locally; only the cloud agent needs ci): the workflow is parked as `.github/workflows/build.yml.disabled` (github ignores it). it does, on windows-latest with msvc x86:
  build with a warning scan, import check, layout check, unit tests, smoke test with a screenshot artifact, debug build with a warning scan. the cloud agent can turn it on by renaming it back to `build.yml`.
- **linux-side syntax check** (optional, fast): `tools/linux_check.sh`.

## 2. open items (none blocks the build)

1. **the parked ci workflow never ran.** every script it calls was run locally, but its yaml is unproven (and the smoke step is `continue-on-error`): if it is ever turned on, expect to fix it on the first windows-latest run.
2. **big files are slow to open** because of the stock edit control, not our code: setting the text makes the control build its line index and measure every line (about 44 us per line, ~0.65 s per mb: 20 mb = 13 s,
   50 mb = 31 s, the window shows "not responding" meanwhile). profiled: `DocRead` takes 62 ms for 20 mb, the bar logic ~0 ms, and `WM_SETTEXT` and `EM_SETHANDLE` cost the same.
   fixes would be a chunked loader that pumps messages and shows progress, or our own text view instead of the native edit.
3. **printing was never run against a printer or pdf driver.** the wrap logic is tested, page setup opens; the job runs inside the command handler (no abort dialog, no message pump).
4. dialogs are not re-laid-out when dragged to a monitor with another dpi (the main window is).
5. ime composition in the edit is untested (no ime available to drive it here).
6. the custom radio buttons are both tab stops (native: only the checked one); arrow keys already move and select.
7. windows 11 snap layouts flyout on the maximize button is not available: the strip's buttons are `HTCLIENT` (own mouse handling) so they can't answer `HTMAXBUTTON`.
8. `notepad foo.` on the command line still falls back to `foo.txt`; the open / save dialogs no longer do (`OpenCmdFile` in `main.c`).
9. `tools\layout_check` checks argument byte counts of the api prototypes, not their argument types.
10. no license decision yet (the icon derives from microsoft's notepad icon, see `README.md`).

## 3. definition of done

- [x] `build.bat` clean (zero /W3 warnings), imports only kernel32 / user32 / gdi32, `src/stubs.c` gone
- [x] every menu item does what notepad's does (plus the extras in `README.md`): driven by `tests\ui`, see `NOTES.md`
- [x] `NOTES.md` / `README.md` updated
- [x] ci: switched off on purpose (only the cloud agent needs it); the local equivalent is `tools\verify.bat ui`, all green
- [ ] a pr description that says, per item, how it was verified: the table in `NOTES.md` ("status") is that list; `main` was not pushed (nothing was force-pushed)
