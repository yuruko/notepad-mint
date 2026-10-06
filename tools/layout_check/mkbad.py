#!/usr/bin/env python3
"""mkbad.py - writes a deliberately broken copy of w32.h for the negative test (see README.md).

    python mkbad.py [--all] [w32.h [out.h]]      default: src\\w32.h -> build\\layout_check\\bad\\w32.h

three breaks: the field order of one struct, the value of one constant, the argument count of one api.
check.bat on the copy has to report all three and fail.
--all adds breaks for the other categories (lost struct packing, a macro, a typedef, a pointer-valued constant, an
enumerator, a cdecl api, an api that is not exported) and two names the sdk does not have (UNVERIFIED lines);
it writes build\\layout_check\\bad\\w32_all.h so the three-break copy stays as it is.
"""
import os
import re
import sys

# (regex, replacement): each regex must match exactly once
BREAKS = [
    (r"(UINT message;\s*)WPARAM wParam;(\s*)LPARAM lParam;", r"\1LPARAM lParam;\2WPARAM wParam;"),      # MSG: field order
    (r"(#define\s+WM_PAINT\s+)0x000F", r"\g<1>0x000E"),                                                  # constant value
    (r"(SetTimer\(HWND,\s*UINT_PTR,\s*UINT),\s*void\s*\*\)", r"\1)"),                                    # api arity (16 -> 12)
]

EXTRA_BREAKS = [
    (r"#pragma\s+pack\(push,\s*1\)", "#pragma pack(push, 4)"),                                           # PRINTDLGW loses its 1-byte packing
    (r"(#define\s+GET_X_LPARAM\(lp\)\s+)\(\(int\)\(short\)LOWORD\(lp\)\)", r"\1((int)LOWORD(lp))"),      # macro: no sign extension
    (r"typedef\s+short(\s+)SHORT;", r"typedef unsigned short\1SHORT;"),                                  # typedef signedness
    (r"(#define\s+HWND_TOPMOST\s+\(\(HWND\)\(LONG_PTR\)\s*)-1\)", r"\g<1>-2)"),                          # pointer-valued constant
    (r"(#define\s+DWMWA_BORDER_COLOR\s+)34", r"\g<1>33"),                                                # enumerator in the sdk
    (r"(wsprintfW\(LPWSTR,\s*LPCWSTR),\s*\.\.\.\)", r"\1)"),                                             # cdecl api declared stdcall
    (r"WINAPI\s+IsChild\(", "WINAPI IsChildX("),                                                         # name that is not exported
]

# names the sdk does not have: cmp prints them as UNVERIFIED (and gen.py prints the macro without test vectors)
EXTRA_LINES = ["#define LC_TEST_UNVERIFIED 7", "#define LC_TEST_UNVERIFIED_MACRO(x) ((x) + 1)"]


def main():
    args = [a for a in sys.argv[1:] if a != "--all"]
    allbreaks = len(args) != len(sys.argv) - 1
    root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    src = args[0] if len(args) > 0 else os.path.join(root, "src", "w32.h")
    dst = args[1] if len(args) > 1 else os.path.join(root, "build", "layout_check", "bad",
                                                     "w32_all.h" if allbreaks else "w32.h")
    with open(src, encoding="utf-8", newline="") as f:
        text = f.read()
    breaks = BREAKS + (EXTRA_BREAKS if allbreaks else [])
    for pat, rep in breaks:
        text, n = re.subn(pat, rep, text)
        if n != 1:
            sys.stderr.write("mkbad: %d matches (expected 1) for %s\nmkbad: w32.h changed, update the patterns\n" % (n, pat))
            return 1
    if allbreaks:
        i = text.rfind("#endif")
        if i < 0:
            sys.stderr.write("mkbad: no #endif in %s\n" % src)
            return 1
        nl = "\r\n" if "\r\n" in text else "\n"
        text = text[:i] + nl.join(EXTRA_LINES) + nl + text[i:]
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    with open(dst, "w", encoding="utf-8", newline="") as f:
        f.write(text)
    print("mkbad: wrote %s (%d breaks%s)" % (dst, len(breaks), " + 2 extra names" if allbreaks else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
