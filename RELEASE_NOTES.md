notepad mint 1.0.12

**installer**
- the last checkbox on the finish page is now just "set as default text editor" and fits on one line
- fixed: that checkbox was set up on the wrong installer page, so it was always ticked: on an update it opened default apps again, and it opened them even when "text file types" was unticked. it now follows the component and stays unticked on an update that already had the file types
- "run notepad mint" starts the editor as you, not elevated (an elevated editor can't take files dragged from explorer)
- uninstalling removes the per-user "set as default" entries only when they belong to the uninstalled copy, so a portable exe elsewhere keeps its own

**faster**
- repainting a small part of the editor (typing a character, moving the caret, a selection) costs half as much in a big window: 15.5 to 7.3 ms at 1772 x 914 (the whole window was drawn off screen each time, now only the part that changes)
- saving a file scans for line breaks with sse2 like opening does; the sun / moon / wrap icons in the menu bar are computed once per size, not on every repaint

**fixes and cleanup**
- a real windows "parameter is incorrect" error (87) is no longer shown as "that code page isn't installed"
- the print cursor is restored correctly when no cursor was set yet
- removed duplicated helpers (one `MpNote` for the 9 "ok" messages, one `ClampInt`), unused windows declarations, the editor colours mirrored in the settings struct, stale comments and a magic number

Regression coverage: unit 442, gui 820, probe-build gui 179, printing suite (all pass).

**download**

- `notepad-mint-1.0.12-setup.exe`: installer and uninstaller
- `notepad-mint-1.0.12.exe`: standalone executable
- `SHA256SUMS.txt`: download checksums

settings remain in `%appdata%\notepad-mint\settings.ini`.
