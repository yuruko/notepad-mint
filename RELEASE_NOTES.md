notepad mint 1.0.10

**right to left**
- a document whose first letter is hebrew, arabic or another right to left script opens right to left (text right aligned, scroll bar on the left); typing such a letter into an empty document switches it too, like `dir="auto"`
- ctrl+right shift / ctrl+left shift set right to left / left to right, like notepad; the edit menu item still toggles it, and a picked order stays until the next new or open
- right aligned text is no longer cut off at the right edge, and the left scroll bar is as thin as the right one

**untitled names**
- `mint-XXXX` now counts up from `mint-0000` at midnight on 1 january 2000 (only the last two digits of the year count) in steps of 32 minutes, so names sort by date and time and always start the same way for years (`9...` in 2026)
- every new untitled document gets a higher name than the one before, in every window (the last one handed out is kept in settings.ini)

**notepad features and fixes**
- `/a file` and `/w file` open a file as ansi / utf-16 like notepad; `/p file` prints without flashing the window
- a file with a broken utf-8 sequence after its bom (or an odd number of bytes for utf-16) can be opened after a prompt instead of not at all
- the status bar shows the zoom (`120%`) while ctrl+plus / minus is in effect; click it to go back to 100%, click the position to go to a line
- a window closed later no longer overwrites settings another window changed (theme, font, word wrap ...); settings are saved at logoff / shutdown too; a window closed while minimized remembers it was maximized
- a file in file > recent that fails to open for a moment (locked, cancelled) stays in the list; only a missing file leaves it
- reopen with encoding: asks about unsaved changes first, and is grayed for an untitled document
- f5 in the find box no longer inserts the time into the document; ctrl+shift shortcuts (ctrl+shift+n ...) no longer change the reading order; alt+z keeps a backwards selection's caret where it was; enter opens the about box link; right arrow on a submenu item moves to the next menu; the scroll bars follow a dpi change; help topics shows f1

**faster**
- searching while ignoring case in non-latin text (cyrillic, greek ...) uses a lower case table instead of a system call per character: about 5.6x faster (18.4 to 3.3 ms per million characters)
- a new sse2 routine finds the next possible first character of a search and the line breaks of a file being opened

Regression coverage: new gui tests for right to left (T44, 22 checks) and the command line switches (T45); unit tests for the new name clock (every day of the century), the sse2 routine (every hit position and a protected page) and the lower case table (all 65,536 units).

**download**

- `notepad-mint-1.0.10-setup.exe`: installer and uninstaller
- `notepad-mint-1.0.10.exe`: standalone executable
- `SHA256SUMS.txt`: download checksums

settings remain in `%appdata%\notepad-mint\settings.ini`.
