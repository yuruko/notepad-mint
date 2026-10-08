notepad mint 1.0.6

- editor repaints are buffered as complete frames, keeping text visible while multilingual glyphs are drawn during resize
- the font preview uses one line at the actual selected size, from 9 to 70 pt: "sphinx of black quartz, judge my vow. 0123456789"
- smaller executable: 134,656 bytes, 6.7% smaller than 1.0.5, through lossless icon packing and link-time optimization
- nearby caret movement avoids rescanning the whole document prefix; utf-8 loading reuses validation, and crlf byte sizing skips an unused newline scan
- selection printing copies only selected text; zero-capacity path handling is guarded

regression coverage includes multilingual painting, partial repaints, resource lifetime, every preview size in both themes, randomized caret/status comparisons, encoding and file failures, and selection-print allocation failures.

**download**

- `notepad-mint-1.0.6-setup.exe`: installer and uninstaller
- `notepad-mint-1.0.6.exe`: standalone executable
- `SHA256SUMS.txt`: download checksums

settings remain in `%appdata%\notepad-mint\settings.ini`.
