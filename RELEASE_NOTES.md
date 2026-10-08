notepad mint 1.0.7

- resizing the window or changing zoom scrolls the text caret into view, including wrapped multilingual text and backward selections
- caret visibility follows deferred wrapping and scrollbar changes while preserving document text, selection and undo
- source reorganized into focused modules for preferences, document state, status calculations, caret geometry/text helpers, buttons, dialogs and menu definitions, with clearer ownership comments
- fixed repeated-DPI dialog font leaks, retained valid help-window fonts, and released the scrollbar pattern bitmap
- print and page setup dialogs now hold the modeless find window so it cannot change the document during the operation
- button painting falls back to direct drawing when buffering resources are unavailable; document statistics retry failed text-buffer locks

Regression coverage includes actual caret geometry during resize and zoom, selection/undo preservation, rapid wrapping, repeated DPI resource ownership and native-dialog success/cancellation/error/retry paths. Smoke and GUI checks run on an isolated desktop with temporary settings.

**download**

- `notepad-mint-1.0.7-setup.exe`: installer and uninstaller
- `notepad-mint-1.0.7.exe`: standalone executable
- `SHA256SUMS.txt`: download checksums

settings remain in `%appdata%\notepad-mint\settings.ini`.
