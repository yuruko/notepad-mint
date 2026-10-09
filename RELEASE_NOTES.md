notepad mint 1.0.11

**default text editor**
- the installer has a new component, "text file types" (on by default): notepad mint is listed under *open with* and in *settings > default apps* for .txt .log .ini .cfg .conf .md .csv .nfo .diz and .text. nothing changes until you pick it: windows lets only you choose a default program, so the finish page offers to open *default apps* on notepad mint, where you set it (newer windows 11 builds have one "set default" button there, older ones a choice per extension)
- help > set as default text editor does the same for the current user (no admin; works for the standalone exe too) and opens that settings page
- the uninstaller removes all of it (only notepad mint's own entries)

**untitled names**
- `mint-0000` is now midnight on 1 january 2026 (was 2000), still 32 minute steps and a 100 year cycle; today's names start with `09`. a counter left by 1.0.10 resyncs to the clock by itself

Regression coverage: the per-user registration is checked against a throwaway registry key (this pc's real associations are never touched); the name clock is checked for every day from 2026 to 2125, including 2100 (no leap year).

**download**

- `notepad-mint-1.0.11-setup.exe`: installer and uninstaller
- `notepad-mint-1.0.11.exe`: standalone executable
- `SHA256SUMS.txt`: download checksums

settings remain in `%appdata%\notepad-mint\settings.ini`.
