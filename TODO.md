# TODO - finishing notepad mint

this file is the work order for whoever continues the project (people or agents). read `README.md` and `NOTES.md` first (design + decisions),
then `src/mp.h` (all shared declarations), `src/ui.c` (dialog scaffolding, `MsgDlg` at the bottom is the canonical dialog), `src/edit.c`, `src/main.c`.

## 0. hard rules (breaking any of these breaks the build or the product)

- **raw c17 + x86 masm, 32-bit only.** no crt, no `windows.h`, no third-party code. the exe links with `/NODEFAULTLIB`, so any crt call
  (`strlen`, `wcslen`, `malloc`, `printf`, `qsort`, `swprintf` ...) is an *unresolved external at link time*. `memset/memcpy/memmove/memcmp` come from `src/rt.asm`.
  use the helpers in `src/util.c` (`mem_alloc`, `wcopy`, `wcat`, `wcmp`, `wcmpi`, `wlow`, `wtoi`, `PathName/PathDir/PathJoin`, `ParseColor/FormatColor` ...) and win32 (`wsprintfW`, `lstrlenW` ...).
  **no 64-bit integer division / modulus / shifts and no floating point** (they need crt helpers such as `__alldiv`); use `MulDiv` for scaled math.
- every win32 function, struct and constant you use must be declared in `src/w32.h` with the exact **32-bit sdk** signature / layout (stdcall, `API` = dllimport).
  only kernel32 / user32 / gdi32 may be imported statically; anything else (dwmapi, uxtheme, shell32, comdlg32 ...) is loaded with `LoadLibraryW` + `GetProcAddress` (see `UiInit` in `ui.c`).
  win10-only apis must be looked up at run time too (see `UiDpiForWindow`).
- **all user-visible text is lowercase** (labels, buttons, titles, messages).
- dpi: coordinates you hand to the ui helpers (`UiLabel/UiEdit/UiButton/DlgFrame/DlgOpen`) are 96-dpi pixels; any raw pixel math goes through `S()`.
- fonts: dialogs use `g_fontUI` (segoe ui 9pt, `DEFAULT_CHARSET`), the main window chrome uses `g_fontMenu`. **never use tahoma / microsoft sans serif for ui text**: on a japanese
  system locale (the dev machine is ja-JP) they draw `\` as a yen sign in every charset (measured, see `tools/fonttest.c`).
- a custom window class that handles `WM_NCCREATE` must still call `DefWindowProcW` for it (that is what stores the window text, otherwise buttons are blank).
- keep `build.bat` producing `build\notepad mint.exe` with **zero warnings at /W3**; keep the exe's imports to kernel32 / user32 / gdi32 only.
- dialogs follow the `MsgDlg` pattern in `ui.c`: a struct whose first member is `DlgBase`, one window class per dialog registered once with `RegClass`, a wndproc that starts with
  `DlgBase *b = DlgFromHwnd(h, m, l); if (!b) return DefWindowProcW(...)` and ends with `if (DlgCommon(b, m, w, l, &r)) return r;`. open with `DlgOpen(b, cls, title, cw, ch, modeless)`,
  run modal ones with `DlgRunModal(b)`. default button id = `IDOK`, esc = `IDCANCEL`. native `COMBOBOX` / trackbar / comctl32 controls cannot be darkened: do not use them.
  native `LISTBOX` is fine but create it owner-draw (`LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | LBS_NOTIFY`), draw the selection with `C_ACCENT` / `C_FACE`, call `DarkScroll()` on it.
  dropdown-like choices = an `mp_btn` that pops `MenuPopup()` (ids >= 1000 so they never collide with `IDM_*`).

## 1. you can't run the app (probably linux): the feedback loop

1. **ci first**: `.github/workflows/build.yml` builds on `windows-latest` with the real msvc x86 toolchain (`build.bat`), uploads the exe, and has a best-effort smoke test.
   push your branch and read the result (`gh run list / gh run view --log-failed`, the pr checks, or the api, whatever you have). iterate until green.
   extend the workflow: assert the exe's imports are only kernel32 / user32 / gdi32 (`dumpbin /imports`), run the unit tests from section 4, keep the smoke test honest.
2. **linux-side syntax check** (optional but fast): a 32-bit mingw-w64 cross compiler can check most files: e.g.
   `i686-w64-mingw32-gcc -std=gnu17 -fsyntax-only -ffreestanding -D__int64="long long" -Wall src/find.c` (you may need a few `-D` shims; do not change the sources to please gcc if msvc is fine with them).
3. if you cannot observe a ci result at all, say so plainly in the pr and list what is unverified. never claim something was tested when it was only read.

## 2. the missing modules (the menu items currently hit the placeholders in `src/stubs.c`)

each is one new file with the functions already declared in `src/mp.h`. as each lands, delete its placeholder from `stubs.c`; delete `stubs.c` when empty. they are independent: do them in parallel,
but only one person/agent edits the shared files (`mp.h`, `w32.h`, `stubs.c`, `NOTES.md`, `README.md`, `build.bat`) at a time.

### 2.1 `src/find.c` - find / replace (modeless) + go to
declared: `void FindDlgShow(int replaceMode); void FindNext(int dirUp); HWND FindDlgHwnd(void); BOOL FindHasText(void); void GotoDlg(HWND owner);`
state: static `WCHAR g_findWhat[256], g_findWith[256]; int g_findUp;` match case / wrap around live in `g_pf.matchCase` / `g_pf.wrapAround` (main saves them).
- `FindDlgShow`: **modeless** dialog owned by `g_hwnd` (`DlgOpen(..., modeless=1)`), main routes keys with `IsDialogMessageW(FindDlgHwnd(), &msg)` so `FindDlgHwnd()` must be NULL when it doesn't exist.
  if it exists: switch mode (destroy + recreate keeping the text) or just bring it to front; focus "find what" with its text selected. prefill "find what" from the main edit's selection when it is
  non-empty, has no line break and is <= 255 chars (`EM_GETSEL` + `EditLockText`; never the clipboard).
  layout (client px), notepad-like: find mode ~410x130: label "find what:" + edit (1001); checks "match case" (1002), "wrap around" (1003); an etched `DlgFrame` "direction" with radios "up" (1004) / "down" (1005);
  buttons "find next" (`IDOK`, default) / "cancel". replace mode ~410x170 adds "replace with:" + edit (1006) and buttons "find next", "replace" (1007), "replace all" (1008), "cancel". titles "find" / "replace".
  find next / replace / replace all are disabled while "find what" is empty.
- find next: search from the END of the selection going down (START going up); if nothing and wrap around is on, continue from the other end; if still nothing:
  `MpAsk(dlg, APP_NAME, L"cannot find \"<text>\"", L"ok", NULL, NULL, 1)`. on a hit: `EM_SETSEL(start,end)` + `EM_SCROLLCARET` on `g_edit`; keep the dialog open (the edit has `ES_NOHIDESEL`, so its selection stays visible).
- replace: if the selection equals the find text (honouring match case) `EM_REPLACESEL(TRUE, with)`, then find next; else just find next.
- replace all: one pass over the zero-copy view (`EditLockText` / `EditUnlockText`: CRLF text, buffer NOT nul terminated), build the result in one `mem_alloc` buffer, then `EM_SETSEL(0,-1)` +
  `EM_REPLACESEL(TRUE, buf)` so it is one undo step. zero matches => the same "cannot find" message.
- case-insensitive compare with `wlow()` on both sides + a first-char prefilter; case-sensitive = plain compare. keep the search function pure (pointer + length in, index out) so it can be unit tested.
- cancel / close / esc destroys the dialog (`DlgCommon` already `DestroyWindow`s modeless dialogs on `WM_CLOSE`: make `IDCANCEL` send `WM_CLOSE`); clear your statics on `WM_NCDESTROY`, then `SetFocus(g_edit)`.
- `FindNext(dirUp)` (f3 / shift+f3): uses the remembered text + options (f3 = down, shift+f3 = up regardless of the dialog radios); no remembered text => `FindDlgShow(0)`. `FindHasText()` = `g_findWhat[0] != 0`.
- `GotoDlg(owner)`: modal "go to line" (~300x110): label "line number:" + numeric edit prefilled with the caret line (`EditCaretPos`), text selected; buttons "go to" (default) / "cancel".
  ok: `n = wtoi(text)` (< 1 means 1); `if (!EditGotoLine(n))` show `MpAsk(dlg, L"go to line", L"the line number is beyond the total number of lines", L"ok", NULL, NULL, 1)` and stay; else close + `SetFocus(g_edit)`.

### 2.2 `src/filedlg.c` - open / save as / encoding picker (custom dark dialogs; the native ones can't be dark)
declared: `BOOL FileDlgOpen(HWND owner, WCHAR *path, int cap); BOOL FileDlgSave(HWND owner, WCHAR *path, int cap, int *enc, int *eol); BOOL EncDlg(HWND owner, int *enc, int reopen);`
- modal, fixed size ~580x420 client px (save a bit taller). rows: "look in:" + path edit (enter navigates to a typed absolute path) + button "up" (parent; from a drive root go to the drive list) /
  owner-draw listbox: directories first (alphabetical via `wcmpi`, small folder glyph drawn by you), then files matching the filter; skip hidden entries and "."/".."; at the top level list the drives
  (`GetLogicalDrives` / `GetDriveTypeW`, "c:\" style) / "file name:" + edit + default button "open" / "save" / "files of type:" dropdown ("text documents (*.txt)", "all files (*.*)") + "cancel".
  save only: "encoding:" dropdown ("utf-8", "utf-8 with bom", "utf-16 le", "utf-16 be", "ansi" = `ENC_*` 0..4 via `g_encName[]`, and "other code page..." which opens `EncDlg(dlg, &enc, 0)`; show `EncLabel()` of the pick)
  and "line ending:" dropdown (`g_eolName[]`, `EOL_*`). initial values from `*enc` / `*eol`, written back on ok.
- start folder: directory part of `path` if it exists, else the last folder used in this process (static), else the current directory. save: the file-name edit starts with the name part (empty for untitled). initial focus: file name edit.
- single click on a file copies its name to the edit; double click / enter on a directory enters it; double click on a file = ok. backspace in the list = up (nice to have).
- ok (open): wildcard (`*` / `?`) in the name => use it as the filter and refresh; existing directory => navigate; else build the full path (a name with a drive letter / leading backslash / unc prefix is absolute,
  else join with the current folder); missing file => `MpAsk(dlg, L"open", <name> + L"\n\nfile not found. check the file name and try again.", L"ok", NULL, NULL, 1)` and stay; else fill `path` (never overflow `cap`) and return TRUE.
- ok (save): same resolution; **notepad rule: if the last path component has no '.', append ".txt"**; existing file => `MpAsk(dlg, L"save as", <path> + L"\n\nalready exists. do you want to replace it?", L"yes", L"no", NULL, 2)` (anything but 1 stays);
  missing target folder => error + stay. on success write `path`, `*enc`, `*eol`, return TRUE. cancel / esc => FALSE, outputs untouched. titles "open" / "save as".
- filter patterns: small case-insensitive wildcard matcher you write; "all files" = `*.*` matches names without a dot too. paths are `PATH_CAP` (1024) chars max: use `wcopy/wcat/PathJoin` with the right caps.
- `EncDlg(owner, enc, reopen)`: ~400x360, title reopen ? "reopen with encoding" : "other code page"; label (reopen ? "pick the encoding to read the file with:" : "pick the encoding to save the file with:") above an owner-draw listbox of
  every `EncListGet(i, ...)` (`i < EncListCount()`; item data = the encoding id, 0..4 or a code page >= 100); preselect `*enc`; "ok" / "cancel"; double click = ok; writes `*enc` on ok.
- main already does: default `.txt`-less names, the lossy-save prompt, "reopen" (`Reopen()` in `main.c`), settings. the dialogs only pick values.

### 2.3 `src/fontdlg.c` - "font & colors" dialog (`BOOL FontDlg(HWND owner)`)
modal ~640x450, title "font & colors". works on a local copy of `g_pf.font[32] / pt / bold / italic / fg / bg`; on ok copies back and returns TRUE (main then calls `AppApplyPrefs`, which also resets the working size and re-fonts the menus);
cancel returns FALSE.
- left "font": check "monospaced fonts only" (default on); owner-draw listbox of families (`EnumFontFamiliesExW`, `DEFAULT_CHARSET`, skip names starting with '@', de-duplicate case-insensitively, names are `WCHAR[32]`);
  decide "monospaced" robustly (create the font, compare the widths of `L"iiiiiiii"` and `L"WWWWWWWW"`; lazily + cached); sorted with `wcmpi`. "size:" numeric edit + hint "10 to 96 pt" (`IDC_DIM`) + a row of preset buttons
  10 11 12 13 14 16 18 20 24 28 32 36 48 72 96; checks "bold" / "italic". size is clamped to `FONT_MIN..FONT_MAX` (10..96) on leaving the edit and on ok.
- right "colors": blocks "text color" and "background": big swatch, hex edit "rrggbb" (`ParseColor` / `FormatColor`, live, invalid text keeps the old colour), three R/G/B sliders 0..255 with value labels.
  write a small custom window class `mp_slider` (sunken track, mint thumb, mouse drag with capture, arrows/page/home/end, `WM_GETDLGCODE` = `DLGC_WANTARROWS`, notifies the parent with `WM_COMMAND`).
  guard the edit <-> slider <-> preset updates against feedback loops. a row of 12 preset swatches (text/background pairs): mint ffffff/161418 (the default), paper 1b1b1b/f5f5f0, solarized dark 839496/002b36,
  solarized light 657b83/fdf6e3, monokai f8f8f2/272822, dracula f8f8f2/282a36, nord d8dee9/2e3440, gruvbox dark ebdbb2/282828, terminal green 33ff66/000000, amber ffb000/1a1000, high contrast ffffff/000000,
  black on white 000000/ffffff; clicking sets both; show the preset name in a dim label.
- bottom: sunken "preview" panel rendering the current choice (face, point size scaled by `g_dpi`, bold/italic, colours; clamp the drawn size to what fits, ~32pt): line 1 "the quick brown fox jumps over the lazy dog 0123456789",
  line 2 "日本語 한국어 العربية עברית русский ελληνικά", line 3 "int main(void) { return 0; }". repaint on every change.
- buttons "ok" (default), "cancel", "reset" (face "Consolas" then `FontResolve()`, 13 pt, no bold/italic, text ffffff, background 161418). initial focus: the font list with the current face selected and scrolled into view.
- all text lowercase; leak nothing (HFONT / HBRUSH / HDC).

### 2.4 `src/print.c` - print + page setup (`void PrintDoc(HWND owner); void PageSetup(HWND owner);`)
- `comdlg32.dll` is loaded on demand (`LoadLibraryW` + `GetProcAddress("PrintDlgW" / "PageSetupDlgW")`); never statically imported. `PRINTDLGW` (packed to 1 byte on 32-bit) and `PAGESETUPDLGW` are already in `w32.h`. the native dialogs are light-themed: accepted.
- keep `hDevMode` / `hDevNames` in statics (shared by page setup and print), free replaced ones with `GlobalFree` (declare it).
- `PageSetup`: `PSD_MARGINS | PSD_INTHOUSANDTHSOFINCHES`, margins from `g_pf.marginL/T/R/B` (1/1000 inch), write them back on ok.
- `PrintDoc`: `PD_RETURNDC | PD_NOSELECTION | PD_USEDEVMODECOPIESANDCOLLATE` (+ page range if you like). text = `EditGetDocText` (CRLF; free with `mem_free`), job name = file name or "untitled". font = the editor face at the **chosen** size `g_pf.pt`
  (not `g_pf.cur`), bold/italic from `g_pf`, height `-MulDiv(pt, LOGPIXELSY, 72)`. margins -> device px relative to the printable area (`PHYSICALOFFSETX/Y`, `PHYSICALWIDTH/HEIGHT`), clamp so the text box is never empty.
  split at CRLF, expand tabs to 8 columns, word-wrap to the text-box width with `GetTextExtentExPointW` (break at the last fitting space, else hard break; always advance >= 1 char), paginate by line height, compute rows lazily page by page
  (no unbounded allocation). header = file name centred in the top margin, footer = "page <n>" centred in the bottom margin (like notepad; skip if the margin can't hold a line).
  `StartDocW` / `StartPage` / `TextOutW` / `EndPage` / `EndDoc`; any failure => `AbortDoc` + `MpAsk(owner, APP_NAME, L"cannot print the document.", ...)`. copies: if the driver ignores the devmode copies, repeat the job. free everything on every exit path.

### 2.5 integrate
delete the placeholders from `stubs.c` as modules land (and the file when empty), update `NOTES.md` (status section) and `README.md` ("not written yet" list), keep `build.bat` building `src\*.c`.

## 3. other features still owed

- **title bar in the chrome font** (the user asked for it): `src/frame.c` is an experiment that draws a custom title strip (`FRAME_CUSTOM`, currently 0 = off). when switched on, windows still drew its own caption above ours
  (the `WM_NCCALCSIZE` override had no effect, `FrameNcCalc` never logged). find out why (suspects: the system only applies the shrunken client rect when ...; `DwmExtendFrameIntoClientArea`; `SetWindowPos(SWP_FRAMECHANGED)` after creation;
  an earlier handler consuming the message) - only turn it on if a ci screenshot (or other evidence) shows a single title bar, correct maximize / restore / snap behaviour and working min / max / close buttons; otherwise leave it off and write down what you found in `NOTES.md`.
- **scrollbars that only appear when needed - NOT yours**: the maintainer is doing this locally in `src/edit.c` / `src/main.c` (needs a real display). do not touch the scrollbar logic; just don't regress it.
- polish / review pass over what you wrote: ime composition in the edit, per-monitor dpi change while a dialog is open, a 50 mb file, ctrl+backspace / ctrl+delete, rtl toggle, new window cascade, `settings.ini` round trip.

## 4. verification the project still owes (do these in ci where possible)

- **`tools/layout_check`**: prove `src/w32.h` matches the real 32-bit sdk: for every struct in `w32.h` compare `sizeof` + every field offset, for every `#define` that exists in the sdk compare the value, and check every `API` prototype has the right
  stdcall byte count (`_Name@N`). e.g. two translation units (one including the real `windows.h`, one including only `w32.h`) that each export a table, plus a checker that diffs them; run it in the windows ci job
  (a mingw-w64 `windows.h` is also fine on linux if you can run the result). report every mismatch and fix `w32.h`.
- **unit tests** (host-only programs, may use the normal crt): `DocRead` / `DocWrite` round trips for every encoding (`ENC_*` and a few code pages) x every line ending (crlf / lf / cr / mixed -> majority wins), bom handling, utf-16 without bom,
  invalid utf-8 -> ansi, lossy detection on an 8-bit code page, empty file, a file ending without a newline; the pure search function from `find.c` (case, wrap, up/down, crlf boundaries, empty needle);
  `EncListGet` / `EncLabel`; path helpers; wildcard matcher. wire them into the ci job.
- **smoke test in ci**: start the exe with `tests/multilingual-sample.txt`, assert it stays alive and the main window title is `multilingual-sample.txt - notepad mint`; if the runner has a desktop, also capture a screenshot artifact
  (`tools/shot.ps1` is a starting point).

## 5. definition of done

`build.bat` clean (zero /W3 warnings), ci green on windows-latest, imports only kernel32 / user32 / gdi32, `src/stubs.c` gone, every menu item does what notepad's does (plus the extras in `README.md`), `NOTES.md` / `README.md` updated,
and a pr description that says, per item, **how it was verified** (ci job, unit test, read only). open the pr against `main`; do not force-push `main`.
