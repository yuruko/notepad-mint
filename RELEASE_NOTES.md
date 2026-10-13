notepad mint 1.1.0

**cleaner source**
- every comment is gone from `src\`; function braces sit on the same line as the function; statements and conditions are one line each; no blank lines inside functions (10,784 to 9,486 lines)
- the cleanup was checked to change nothing: the release exe, three probe builds and the unit-test exe come out byte for byte identical (pe timestamps masked)

**faster**
- the editor's line height is measured once per font, not on every key, scroll bar update and resize
- the selection highlight in the editor uses the system highlight brush instead of creating and deleting a brush on every draw
- buttons no longer read their size on every mouse move, only while pressed; help > set as default text editor loads advapi32 once instead of twice

**cleanup**
- removed dead code: unreachable `WM_SIZE` cases in the editor, an empty branch and a repeated store in the menus, an unused bevel style and triangle direction, a second `EM_LIMITTEXT`, a redundant scroll bar refresh, unused variables and parameters, an unused list of line ending names, test-only search wrappers (now in the unit tests)
- one error exit in `DocWrite` instead of eight copies of the cleanup, one `OtherFocus` helper for select all / time and date / clear line, the font dialog uses the shared `ClampInt`

Regression coverage: unit 437, gui 820, probe-build gui 179, printing suite (all pass).

**download**

- `notepad-mint-1.1.0-setup.exe`: installer and uninstaller
- `notepad-mint-1.1.0.exe`: standalone executable
- `SHA256SUMS.txt`: download checksums

settings remain in `%appdata%\notepad-mint\settings.ini`.
