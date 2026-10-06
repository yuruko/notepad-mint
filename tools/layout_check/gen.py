#!/usr/bin/env python3
"""gen.py - writes the layout_check tables for one w32.h (see README.md).

    python gen.py <path-to-w32.h> <outdir>

outdir gets
  real_tab.c     the tables, compiled against the real sdk (windows.h)
  w32_tab.c      the same tables, compiled against w32.h only
  api_tab.c      the address of every API prototype in w32.h (linked against the import libs)
  api_names.txt  the prototype names, one per line (for cmp)

anything in w32.h that this script cannot parse is an error, never silently skipped.
"""
import os
import re
import sys

# ------------------------------------------------------------------ knobs --

# names that w32.h #defines but the sdk only has as enumerators (dwmapi.h). evaluated unguarded in both tables.
ENUM_NAMES = [
    "DWMWA_USE_IMMERSIVE_DARK_MODE",
    "DWMWA_WINDOW_CORNER_PREFERENCE",
    "DWMWA_BORDER_COLOR",
    "DWMWA_CAPTION_COLOR",
    "DWMWA_TEXT_COLOR",
    "DWMWCP_DONOTROUND",
]

# object-like #defines that are not values (the aliases like SetWindowLongPtrW are found by their body)
SKIP_CONSTS = {"API", "WINAPI", "CALLBACK", "NULL", "TRUE", "FALSE"}

# function-like #defines that are not win32 macros
SKIP_MACROS = {"COUNTOF", "DECLARE_HANDLE"}

# typedef names to leave out of the types check
SKIP_TYPES = set()

# function-like macros that exist in w32.h and in the sdk, with argument vectors (c text; high-bit and negative
# values are in on purpose). kind "val" compares the result as a signed 64-bit value, so a wrong signedness shows;
# kind "ptr" is for pointer results (compared as a 32-bit address).
_ONE = ["0", "1", "0x7fff", "0x8000", "0xffff", "0x12345678", "0xfedcba98", "0xffffffff", "-1", "-32768", "-65536"]
_TWO = [("0", "0"), ("1", "2"), ("0xffff", "0xffff"), ("0x1234", "0x5678"), ("0x8000", "0"), ("0", "0x8000"),
        ("-1", "-1"), ("-1", "0"), ("0", "-1"), ("32767", "-32768"), ("0x12345678", "0x9abcdef0")]
_LP = ["0", "1", "0x7fff", "0x8000", "0xffff", "0x00018000", "0xffff0001", "0x80008000", "0x12345678", "-1", "-32768"]
_WHEEL = ["0", "0x00780000", "0xff880000", "0x80000000", "0x7fff0000", "0xffffffff", "-1", "-65536"]
_RGB = [("0", "0", "0"), ("255", "255", "255"), ("1", "2", "3"), ("255", "0", "0"), ("0", "255", "0"),
        ("0", "0", "255"), ("128", "64", "32"), ("0x12", "0x34", "0x56"), ("127", "128", "129")]
_COLOR = ["0", "0x00ffffff", "0x00030201", "0x12345678", "0xffffffff", "0x80808080", "0x00ff0000", "0x0000ff00", "-1"]
_RES = ["0", "1", "32512", "32649", "0xffff", "0x10000", "0x12345678", "-1"]

FUNC_MACROS = [
    ("LOWORD", "val", [(a,) for a in _ONE]),
    ("HIWORD", "val", [(a,) for a in _ONE]),
    ("LOBYTE", "val", [(a,) for a in _ONE]),
    ("MAKELONG", "val", _TWO),
    ("MAKELPARAM", "val", _TWO),
    ("MAKEWPARAM", "val", _TWO),
    ("GET_X_LPARAM", "val", [(a,) for a in _LP]),
    ("GET_Y_LPARAM", "val", [(a,) for a in _LP]),
    ("GET_WHEEL_DELTA_WPARAM", "val", [(a,) for a in _WHEEL]),
    ("RGB", "val", _RGB),
    ("GetRValue", "val", [(a,) for a in _COLOR]),
    ("GetGValue", "val", [(a,) for a in _COLOR]),
    ("GetBValue", "val", [(a,) for a in _COLOR]),
    ("MAKEINTRESOURCEW", "ptr", [(a,) for a in _RES]),
]

# --------------------------------------------------------------- parsing --

IDENT = r"[A-Za-z_]\w*"
FNPTR = re.compile(r"\(\s*(?:%s\b\s*)*\*+\s*(%s)\s*\)\s*\(" % (IDENT, IDENT))     # (CALLBACK *name)(
BUILTIN = {"unsigned", "signed", "char", "short", "int", "long", "__int8", "__int16", "__int32", "__int64"}


def die(msg):
    sys.stderr.write("gen: error: %s\n" % msg)
    sys.exit(1)


def strip_comments(s):
    """drops /* */ and // comments (string and char literals are copied); newlines stay"""
    out = []
    i, n = 0, len(s)
    while i < n:
        two = s[i:i + 2]
        c = s[i]
        if two == "/*":
            j = s.find("*/", i + 2)
            if j < 0:
                die("unterminated comment")
            out.append(" " + "\n" * s.count("\n", i, j))
            i = j + 2
        elif two == "//":
            j = s.find("\n", i)
            i = n if j < 0 else j
        elif c == '"' or c == "'":
            j = i + 1
            while j < n and s[j] != c:
                if s[j] == "\\":
                    j += 1
                j += 1
            out.append(s[i:j + 1])
            i = j + 1
        else:
            out.append(c)
            i += 1
    return "".join(out)


def split_top(s):
    """splits on the commas that are not inside (), [] or {}"""
    parts, cur, depth = [], [], 0
    for ch in s:
        if ch in "([{":
            depth += 1
        elif ch in ")]}":
            depth -= 1
        if ch == "," and depth == 0:
            parts.append("".join(cur))
            cur = []
        else:
            cur.append(ch)
    parts.append("".join(cur))
    return [p.strip() for p in parts]


def decl_name(d):
    """the name one declarator declares: 'x', '*p', 'a[32]', '(CALLBACK *fn)(int)'. the first declarator of a
    declaration also carries the type specifiers, so the name is the last identifier."""
    d = d.strip()
    if ":" in d:
        die("bit-field is not supported: %r" % d)
    m = FNPTR.search(d)
    if m:
        return m.group(1)
    d = re.sub(r"\[[^\]]*\]", " ", d)
    ids = re.findall(IDENT, d)
    if not ids:
        die("no name in declaration %r" % d)
    return ids[-1]


def scan_directives(clean):
    """returns (defs, order, text, notes): defs maps name -> (params or None, body), text is the source without
    its preprocessor lines (pragma pack lines included: the compiler applies those, we only read the names)."""
    joined = clean.replace("\\\r\n", " ").replace("\\\n", " ")
    defs, order, out, notes = {}, [], [], []
    nconds = 0
    for ln in joined.split("\n"):
        s = ln.strip()
        if not s.startswith("#"):
            out.append(ln)
            continue
        out.append("")
        m = re.match(r"#\s*define\s+(%s)(\(([^)]*)\))?\s*(.*)$" % IDENT, s)
        if m:
            name = m.group(1)
            if name in defs:
                notes.append("%s is defined twice in w32.h (the last one wins)" % name)
            else:
                order.append(name)
            defs[name] = (m.group(3) if m.group(2) else None, m.group(4).strip())
            continue
        m = re.match(r"#\s*undef\s+(%s)" % IDENT, s)
        if m:
            defs.pop(m.group(1), None)
            if m.group(1) in order:
                order.remove(m.group(1))
            continue
        m = re.match(r"#\s*(if|ifdef|ifndef|elif|else)\b", s)
        if m:
            nconds += 1
            if not (nconds == 1 and m.group(1) == "ifndef"):          # the first #ifndef is the include guard
                notes.append("w32.h has a conditional (%s): it is not evaluated, every branch is read" % s)
    return defs, order, "\n".join(out), notes


def struct_fields(body, sname):
    fields = []
    for decl in body.split(";"):
        decl = decl.strip()
        if not decl:
            continue
        for d in split_top(decl):
            fields.append(decl_name(d))
    if not fields:
        die("struct %s has no fields" % sname)
    if len(set(fields)) != len(fields):
        die("struct %s declares a field twice" % sname)
    return fields


def snippet(text, pos):
    """the statement that starts at pos, shortened, for error messages"""
    end = text.find(";", pos)
    return " ".join(text[pos:pos + 90 if end < 0 else min(end + 1, pos + 90)].split())


def parse_structs(text):
    """every 'typedef struct [tag] { ... } NAME;'. returns ([(name, fields)], [pointer typedef names],
    the offsets of the typedef keywords that were read)"""
    structs, ptrs, starts = [], [], set()
    head = re.compile(r"\btypedef\s+struct\b")
    opener = re.compile(r"\s*(?:%s)?\s*\{" % IDENT)
    pos = 0
    while True:
        m = head.search(text, pos)
        if not m:
            break
        mo = opener.match(text, m.end())
        if not mo:                                    # 'typedef struct tag NAME;' is read as a plain typedef
            pos = m.end()
            continue
        i = mo.end()
        depth, j = 1, i
        while j < len(text) and depth:
            if text[j] == "{":
                depth += 1
            elif text[j] == "}":
                depth -= 1
            j += 1
        if depth:
            die("unbalanced braces in a struct")
        body = text[i:j - 1]
        k = text.find(";", j)
        if k < 0:
            die("struct without a terminating ';': %s" % snippet(text, m.start()))
        names = []
        for d in split_top(text[j:k]):
            if not d:
                die("empty declarator after a struct body: %s" % snippet(text, m.start()))
            if d.startswith("*"):
                ptrs.append(decl_name(d))
            else:
                names.append(decl_name(d))
        if not names:
            die("struct without a typedef name: %s" % snippet(text, m.start()))
        if "{" in body:
            die("struct %s: a struct or union inside a struct is not supported" % names[0])
        fields = struct_fields(body, names[0])
        for n in names:
            structs.append((n, fields))
        starts.add(m.start())
        pos = k + 1
    return structs, ptrs, starts


def parse_typedef(stmt, kinds):
    """one 'typedef ...;' statement -> [(name, kind)], kind is 'arith', 'ptr' or 'other'"""
    stmt = stmt.strip()
    m = FNPTR.search(stmt)
    if m:
        return [(m.group(1), "ptr")]
    chunks = split_top(stmt)
    first = re.sub(r"\[[^\]]*\]", " ", chunks[0])
    ids = list(re.finditer(IDENT, first))
    if len(ids) < 2:
        die("cannot parse typedef %r" % stmt)
    spec = first[:ids[-1].start()]
    spec_ids = [s for s in re.findall(IDENT, spec) if s not in ("const", "volatile", "__unaligned")]
    if any(s in ("struct", "union", "enum") for s in spec_ids) or spec_ids == ["void"]:
        base = "other"
    elif spec_ids and all(s in BUILTIN for s in spec_ids):
        base = "arith"
    elif len(spec_ids) == 1 and spec_ids[0] in kinds:
        base = kinds[spec_ids[0]]
    else:
        die("typedef %r: unknown base type %r" % (stmt, " ".join(spec_ids)))
    out = []
    for i, c in enumerate(chunks):
        star = ("*" in spec) if i == 0 else c.lstrip().startswith("*")
        kind = "ptr" if star else base
        if "[" in c:
            kind = "other"
        out.append((decl_name(c), kind))
    return out


def parse(path):
    try:
        with open(path, encoding="utf-8-sig") as f:
            src = f.read()
    except (OSError, UnicodeDecodeError) as e:
        die("cannot read %s: %s" % (path, e))
    defs, order, text, notes = scan_directives(strip_comments(src))
    notes = list(notes)

    structs, sptrs, read = parse_structs(text)
    if not structs:
        die("no 'typedef struct { ... } NAME;' found in %s" % path)
    if len({n for n, _ in structs}) != len(structs):
        die("a struct typedef name appears twice")

    # typedefs and DECLARE_HANDLE lines, in file order (an alias needs its base known first)
    kinds = {n: "other" for n, _ in structs}
    types = []
    for m in re.finditer(r"\btypedef\b([^;{}]*);|\bDECLARE_HANDLE\s*\(\s*(%s)\s*\)\s*;" % IDENT, text):
        if m.group(2):
            items = [(m.group(2), "ptr")]
        else:
            read.add(m.start())
            items = parse_typedef(m.group(1), kinds)
        for name, kind in items:
            kinds[name] = kind
            if name not in SKIP_TYPES and name not in {t for t, _ in types}:
                types.append((name, kind))
    for n in sptrs:
        kinds[n] = "ptr"
        types.append((n, "ptr"))
    # every typedef keyword must have been read (a typedef of a union or enum, say, stops the run)
    for m in re.finditer(r"\btypedef\b", text):
        if m.start() not in read:
            die("cannot parse: %s" % snippet(text, m.start()))

    # api prototypes: every 'API' token must start one
    api_re = re.compile(r"\bAPI\b\s+([^;{}()]*?)\bWINAPI\s+(%s)\s*\(([^;{}]*)\)\s*;" % IDENT)
    found = list(api_re.finditer(text))
    apis = [m.group(2) for m in found]
    covered = {m.start() for m in found}
    for m in re.finditer(r"\bAPI\b", text):
        if m.start() not in covered:
            die("cannot parse as 'API ret WINAPI Name(...);': %s" % snippet(text, m.start()))
    if len(set(apis)) != len(apis):
        die("an API prototype appears twice")
    apiset = set(apis)

    # constants: object-like defines with a value
    consts = []
    for name in order:
        params, body = defs[name]
        if params is not None or name in SKIP_CONSTS or not body:
            continue                                   # function-like, compiler-ish, include guard
        if re.fullmatch(IDENT, body) and body in apiset:
            continue                                   # SetWindowLongPtrW -> SetWindowLongW style alias
        consts.append((name, name in ENUM_NAMES))
    cnames = {n for n, _ in consts}
    for n in ENUM_NAMES:
        if n not in cnames:
            notes.append("ENUM_NAMES lists %s, which w32.h does not define" % n)

    # function-like macros
    macros, untested = [], []
    known = set()
    for name, kind, vecs in FUNC_MACROS:
        known.add(name)
        if name in defs and defs[name][0] is not None:
            macros.append((name, kind, vecs))
    for name in order:
        if defs[name][0] is not None and name not in known and name not in SKIP_MACROS:
            untested.append(name)

    return {"structs": structs, "types": types, "apis": apis, "consts": consts, "macros": macros,
            "untested": untested, "notes": notes}


# -------------------------------------------------------------- emitting --

HELPERS = """\
#include "tabdefs.h"

#define LC_OFFSETOF(T, f) ((unsigned)(ULONG_PTR)&(((T *)0)->f))
#define LC_FSIZE(T, f)    ((unsigned)sizeof(((T *)0)->f))
#define LC_CONST(x)       ((unsigned long long)(unsigned int)(ULONG_PTR)(x))
#define LC_VAL(x)         ((long long)(x))
#define LC_PTRVAL(x)      ((long long)(unsigned int)(ULONG_PTR)(x))
#define LC_SIGN(T)        ((((T)-1) < ((T)0)) ? 1 : 0)
"""


def tables(p, real, m):
    """the table definitions with the name prefix p. real: guard each constant / macro with #ifdef (the sdk may
    not define it as a macro); w32.h defines all of them, so there the unguarded form is also a parse check."""
    o = []
    for name, fields in m["structs"]:
        o.append("static const TabField %sf_%s[] = {" % (p, name))
        for f in fields:
            o.append('    { "%s", LC_OFFSETOF(%s, %s), LC_FSIZE(%s, %s) },' % (f, name, f, name, f))
        o.append("    { 0 }")
        o.append("};")
    o.append("")
    o.append("const TabStruct %sstructs[] = {" % p)
    for name, fields in m["structs"]:
        o.append('    { "%s", (unsigned)sizeof(%s), %sf_%s },' % (name, name, p, name))
    o.append("    { 0 }")
    o.append("};")
    o.append("")
    o.append("const TabConst %sconsts[] = {" % p)
    for name, is_enum in m["consts"]:
        guard = real and not is_enum
        if guard:
            o.append("#ifdef %s" % name)
        o.append('    { "%s", LC_CONST(%s) },' % (name, name))
        if guard:
            o.append("#endif")
    o.append("    { 0 }")
    o.append("};")
    o.append("")
    o.append("const TabMacro %smacros[] = {" % p)
    for name, kind, vecs in m["macros"]:
        if real:
            o.append("#ifdef %s" % name)
        for v in vecs:
            call = "%s(%s)" % (name, ", ".join(v))
            o.append('    { "%s", %s(%s) },' % (call, "LC_PTRVAL" if kind == "ptr" else "LC_VAL", call))
        if real:
            o.append("#endif")
    o.append("    { 0 }")
    o.append("};")
    o.append("")
    o.append("const TabType %stypes[] = {" % p)
    for name, kind in m["types"]:
        sign = "LC_SIGN(%s)" % name if kind == "arith" else "-1"
        o.append('    { "%s", (unsigned)sizeof(%s), %s },' % (name, name, sign))
    o.append("    { 0 }")
    o.append("};")
    return "\n".join(o) + "\n"


def write(outdir, name, text):
    with open(os.path.join(outdir, name), "w", encoding="utf-8", newline="\n") as f:
        f.write(text)


def main():
    if len(sys.argv) != 3:
        sys.stderr.write("usage: gen.py <path-to-w32.h> <outdir>\n")
        return 2
    hpath = os.path.abspath(sys.argv[1])
    outdir = sys.argv[2]
    if not os.path.isfile(hpath):
        die("no such file: %s" % hpath)
    os.makedirs(outdir, exist_ok=True)
    inc = hpath.replace("\\", "/")
    m = parse(hpath)

    for n in m["notes"]:
        print("gen: note: %s" % n)
    for n in m["untested"]:
        print("UNVERIFIED MACRO %s (function-like macro with no test vectors in gen.py)" % n)

    stamp = "/* generated by tools/layout_check/gen.py from %s - do not edit */\n" % inc
    real_head = (stamp +
                 "/* the tables, compiled against the real 32-bit windows sdk */\n"
                 "#define UNICODE\n#define _UNICODE\n"
                 "#include <windows.h>\n#include <windowsx.h>\n#include <commdlg.h>\n#include <cderr.h>\n"
                 "#include <dwmapi.h>\n")
    w32_head = (stamp +
                "/* the same tables, compiled against w32.h only (no windows.h, no crt) */\n"
                '#include "%s"\n' % inc)
    write(outdir, "real_tab.c", real_head + HELPERS + "\n" + tables("real_", True, m))
    write(outdir, "w32_tab.c", w32_head + HELPERS + "\n" + tables("w32_", False, m))

    api = [stamp,
           "/* the address of every API prototype in w32.h. linked (/dll /noentry) against kernel32 / user32 / gdi32:\n"
           " * a wrong argument byte count or a name that is not exported is an unresolved external symbol. */\n",
           '#include "%s"\n\n' % inc,
           "const void *api_addrs[] = {\n"]
    api += ["    (const void *)%s,\n" % n for n in m["apis"]]
    api.append("    0\n};\n")
    write(outdir, "api_tab.c", "".join(api))
    write(outdir, "api_names.txt", "".join(n + "\n" for n in m["apis"]))

    nf = sum(len(f) for _, f in m["structs"])
    nvec = sum(len(v) for _, _, v in m["macros"])
    print("gen: %s -> %d structs (%d fields), %d constants (%d enum names), %d macros (%d vectors), %d types, %d api"
          % (inc, len(m["structs"]), nf, len(m["consts"]), sum(1 for _, e in m["consts"] if e),
             len(m["macros"]), nvec, len(m["types"]), len(m["apis"])))
    return 0


if __name__ == "__main__":
    sys.exit(main())
