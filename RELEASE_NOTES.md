notepad mint 1.3.0

- undo and redo with a real history: up to 128 steps. ctrl+z undoes, ctrl+y or ctrl+shift+z redoes, and both are in the edit menu and the right-click menu
- a run of typing is one step (a new step starts after a line break, an arrow or page key, a click, or a save); a run of backspaces or deletes is one step; replace all, cut, paste, clear line and drag and drop are one step each
- undo puts the caret and the selection back where they were: undoing a typed-over word selects the word again
- the history survives toggling word wrap, and undoing back to the saved text still clears the modified mark at any depth
- a new edit after an undo drops the redo steps, like every editor; opening or creating a file starts a fresh history

**download**

- `notepad-mint-1.3.0-setup.exe`: installer and uninstaller
- `notepad-mint-1.3.0.exe`: standalone executable
- `SHA256SUMS.txt`: download checksums

settings remain in `%appdata%\notepad-mint\settings.ini`.
