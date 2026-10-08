notepad mint 1.0.8

- mouse-wheel and scrollbar scrolling keep the cursor and selection at their original text positions
- pressing an arrow, home/end or page up/down after scrolling returns the view to the saved cursor before moving it; up/down move from the original line
- shift+arrow keeps the original selection anchor, and ctrl+up/down retain their native behavior without revealing the cursor

Regression coverage drives the real editor on an isolated desktop, including scrolling in both directions, wrapped multilingual text, backward selections, cursor visibility and undo preservation.

**download**

- `notepad-mint-1.0.8-setup.exe`: installer and uninstaller
- `notepad-mint-1.0.8.exe`: standalone executable
- `SHA256SUMS.txt`: download checksums

settings remain in `%appdata%\notepad-mint\settings.ini`.
