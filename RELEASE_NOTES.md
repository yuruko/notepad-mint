notepad mint 1.2.0

**drag and drop like notepad**
- drop a file on the window and it opens: the first one in this window (after the save prompt), the others in new windows; folders are skipped, and more than 10 files asks first
- hold shift while dropping to insert the paths at the drop point instead, one per line (what a plain drop did before)
- drag selected text to move it, hold ctrl to copy it; drag text in from other programs and out to them (a move takes it out of this document); one undo takes a drop back
- a thin mark shows where the text will land, and the editor scrolls while you hold it near an edge
- the exe still imports only kernel32 / user32 / gdi32: ole32 is loaded once the window has painted

**windows xp notepad parity**
- page setup has a header and a footer: `&f` file name, `&p` page number, `&d` date, `&t` time, `&l` / `&c` / `&r` alignment, `&&` an ampersand; defaults `&f` and `page &p`, remembered
- a small window with a cancel button stays up while a document prints
- `/pt file printer driver port` prints on a named printer; the `print` and `printto` verbs are registered by the installer and by help > set as default text editor
- f3 searches in the direction picked in the find dialog, shift+f3 the other way; the search text, the replacement and the direction are remembered across sessions
- the `&` underlines in the menus, dialogs and buttons stay hidden until you press alt or use the keyboard (always shown if windows is set to); find, replace, go to, font and message box buttons have them
- a double click on the window icon closes the window; the tilt wheel scrolls sideways; `notepad-mint name` for a missing name without an extension offers to create `name.txt`
- pasted text gets windows line breaks (lone lf / cr from other programs no longer run lines together)

**saving through links**
- a symbolic link saves into its target and stays a link
- a file with several hard links, or in a folder that refuses new files, is written in place, so every name sees the change

**readable source**
- cryptic local, parameter and struct member names were renamed (`h, m, w, l` to `hwnd, msg, wParam, lParam` and so on); the exe came out byte for byte identical

Regression coverage: unit 451, gui 839, probe-build gui 217, printing suite 42 (all pass).

**download**

- `notepad-mint-1.2.0-setup.exe`: installer and uninstaller
- `notepad-mint-1.2.0.exe`: standalone executable
- `SHA256SUMS.txt`: download checksums

settings remain in `%appdata%\notepad-mint\settings.ini`.
