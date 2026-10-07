a free, dark replacement for `notepad.exe`: plain text only. no ai, no sign-in, no telemetry.

**download**

- `notepad-mint-<version>-setup.exe`: installer (start menu shortcut, optional desktop shortcut, uninstaller)
- `notepad-mint-<version>.exe`: standalone exe, nothing to install

one 32-bit exe (about 140 kb) for 32-bit windows, 64-bit windows and windows on arm.

**what is in it**

- help topics (f1) rewritten: a key table and short paragraphs that wrap by themselves, the text fills the window, esc closes it; the about box shows the real version
- dark (default) and light theme, mint accent; alt+x or the sun / moon button switches
- word wrap (alt+z or the button), tab size 2 / 4 / 8, zoom 10-96 pt
- file > recent: the last 9 files opened or saved, with a "clear list" item at the bottom
- compact status bar (line:column, lines, bytes, line ending, encoding; selection size), every panel only as wide as its text, click the last two to change them
- utf-8 / utf-16 / legacy code pages, windows / unix / classic mac line endings, right-to-left, unicode control characters
- drop files on the window to insert their paths, selected line breaks are visible, text scrolls to the very edge, thin classic scrollbars
- find / replace, go to, font, page setup, print: everything notepad has

settings are in `%appdata%\notepad-mint\settings.ini`. written in raw c and x86 assembly, no crt, imports only kernel32 / user32 / gdi32.
