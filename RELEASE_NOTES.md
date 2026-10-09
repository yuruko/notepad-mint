notepad mint 1.0.9

- default font size is 10 pt
- the font dialog's preset grid is 6 columns by 4 rows, 8 to 48 pt, with half steps from 8 to 13.5 pt
- the font dialog size box accepts 7 to 100 pt, including one decimal digit (for example 8.5)
- sizes are stored in tenths of a point; an older settings.ini size is converted on load
- ctrl+plus / ctrl+minus / ctrl+wheel still step between 8 and 48 pt

Regression coverage: the font dialog, preset grid, typed decimal sizes, the zoom ladder and the saved size are checked on the real editor.

**download**

- `notepad-mint-1.0.9-setup.exe`: installer and uninstaller
- `notepad-mint-1.0.9.exe`: standalone executable
- `SHA256SUMS.txt`: download checksums

settings remain in `%appdata%\notepad-mint\settings.ini`.
