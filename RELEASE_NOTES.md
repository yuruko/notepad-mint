notepad mint 1.0.4

a free, dark replacement for `notepad.exe`: plain text only. no ai, no sign-in, no telemetry.

**new features**

- whole-word find and replace, including backwards search, wrap around and saved preferences
- classic `.LOG` files append a local time/date entry on opening; one undo removes it
- print just the selected text using the native print dialog
- confirmation before overwriting a file changed or deleted outside the app

**reliability**

- saves write and flush a sibling temporary file before replacing the original; failed writes and replacements retain existing data, with a recovery backup for partial-rename failures
- word-wrap toggles preserve the original editor when memory is exhausted; failed loads and text locks cannot mark a missing document saved
- failed and incomplete reads report an error; malformed explicit utf-8 and odd-length utf-16 are rejected without replacing the current document
- stateful legacy encodings detect character loss before saving
- recent-file lists are read and written as complete snapshots; unchanged settings avoid disk writes and unavailable settings files have bounded retries
- `/p` prints only after a successful load, command-line paths ending in a dot avoid the `.txt` fallback, and printer text-output errors abort the job

**performance**

- linear-time find/replace handles repeated-prefix searches about 19-29 times faster in the included benchmark
- revised x86 sse2 line counting is about 9 times faster on newline-heavy text, with sparse-text performance preserved
- dirty-state hashes are reused until the document changes

the executable is 144,384 bytes, uses no crt or third-party libraries, and statically imports only kernel32, user32 and gdi32. benchmark results depend on hardware and text; reproduce them with `tests\bench\build.bat`.

**download**

- `notepad-mint-1.0.4-setup.exe`: installer and uninstaller
- `notepad-mint-1.0.4.exe`: standalone executable
- `SHA256SUMS.txt`: download checksums

one 32-bit executable for 32-bit windows, 64-bit windows and windows on arm. settings remain in `%appdata%\notepad-mint\settings.ini`.

known limits remain documented in the repository: single-level undo, synchronous large-file loading, no tabs or session recovery, and no custom print headers. whole-word boundaries treat supplementary unicode characters conservatively. file-conflict checks use size and modification time; simultaneous changes during a save are not a filesystem transaction with other programs.
