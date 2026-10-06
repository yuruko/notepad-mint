layout_check

proves that src\w32.h (hand-written win32 declarations, no windows.h) matches the real 32-bit windows sdk.

what it checks
- structs: sizeof, and offset + size of every field. the #pragma pack lines count (printdlg is 1-byte packed, bitmapfileheader 2-byte).
- constants: every object-like #define, evaluated against windows.h. names the sdk only has as enumerators are in ENUM_NAMES (gen.py). any other name the sdk does not define as a macro is printed as `UNVERIFIED` (informational, not a failure).
- function-like macros (LOWORD, MAKELONG, RGB, ...) on fixed argument vectors, high-bit and negative values included.
- typedefs: sizeof and signedness.
- api: every `API ... WINAPI Name(...)` prototype is linked against kernel32 / user32 / gdi32. a wrong argument byte count or a name that is not exported is an unresolved external symbol.
not checked: argument and return types of the prototypes (only their stack byte count), and whether a function exists on the oldest supported windows.

run
    tools\layout_check\check.bat                   checks src\w32.h
    tools\layout_check\check.bat path\to\w32.h     checks another copy
needs python 3 and the 32-bit msvc environment (it loads vcvarsamd64_x86.bat when none is active; a x64 environment is refused). works from any directory. output goes to build\layout_check. the last line is `layout_check: clean` or `layout_check: FAILED`; exit code 0 means clean.

how it works
gen.py parses w32.h and writes real_tab.c (includes windows.h) and w32_tab.c (includes only w32.h) with identical tables, and api_tab.c (the address of every prototype). cmp.c diffs the two tables and reads the log of the api link. what gen.py cannot parse stops the run, it is never skipped. a new function-like macro needs argument vectors in FUNC_MACROS (gen.py), else it is printed as `UNVERIFIED`.

ci
the windows job already loads the x86 toolchain (ilammy/msvc-dev-cmd, arch x86), so check.bat skips its own vcvars lookup. it runs as a step of its own in .github/workflows/build.yml, a non-zero exit fails the job:
    - name: layout check (w32.h vs the real sdk)
      shell: cmd
      run: tools\layout_check\check.bat

negative test
`python tools\layout_check\mkbad.py` writes build\layout_check\bad\w32.h with one wrong field order, one wrong constant and one wrong api arity. check.bat on that copy has to report all three and fail. `mkbad.py --all` writes bad\w32_all.h, which also breaks the other categories.
