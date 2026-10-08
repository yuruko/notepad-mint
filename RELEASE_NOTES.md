notepad mint 1.0.5

fixes the editor text disappearing for a frame when resizing the window.

during a resize, windows can request background clearing before the text is painted again. the editor now keeps its existing text visible until painting starts, and still clears backgrounds during normal painting and print-window capture.

regression coverage checks the live editor pixels before a repaint can hide the issue, in dark and light themes, with text selections and deleted characters. the previous release fails the standalone-erase checks; the fix preserves the text without leaving stale pixels.

**download**

- `notepad-mint-1.0.5-setup.exe`: installer and uninstaller
- `notepad-mint-1.0.5.exe`: standalone executable
- `SHA256SUMS.txt`: download checksums

settings remain in `%appdata%\notepad-mint\settings.ini`.
