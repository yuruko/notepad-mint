#!/usr/bin/env bash
# linux_check.sh - fast linux-side pre-push check with clang targeting 32-bit windows (i686-pc-windows-msvc).
# it is NOT a substitute for the msvc build in ci (msvc /W3 warns about different things), it only catches
# the cheap mistakes before a ci round trip:
#   1. every file compiles (c17, -Wall) against w32.h only
#   2. symbol check: every undefined symbol of the checked objects must be defined in src/*.c, exported by
#      src/rt.asm, or be a dllimport (__imp_...). a crt call (strlen, wcslen, sprintf ...), a 64-bit math helper
#      (__alldiv, __aullrem ...) or float support (__fltused) fails here instead of as an unresolved external at link.
#   3. full mode only: no symbol is defined twice.
#
#   tools/linux_check.sh                 all of src/*.c
#   tools/linux_check.sh src/find.c ...  only those files (their symbols are resolved against all of src/*.c)
# env: W32_EXTRA=path.h   force-included after w32.h (declarations not merged into w32.h yet)
#      SKIP=stubs.c       space separated basenames of src/*.c to leave out of the symbol universe
set -u
cd "$(dirname "$0")/.."
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT

CFLAGS=(--target=i686-pc-windows-msvc -std=c17 -O2 -ffreestanding -fms-extensions -Wall
        -Wno-unused-function -Wno-ignored-attributes -Wno-microsoft-anon-tag -include src/w32.h)
[ -n "${W32_EXTRA:-}" ] && CFLAGS+=(-include "$W32_EXTRA")

skip=" ${SKIP:-} "
all=()
for f in src/*.c; do
    case "$skip" in *" $(basename "$f") "*) continue ;; esac
    all+=("$f")
done
if [ $# -gt 0 ]; then mine=("$@"); full=0; else mine=("${all[@]}"); full=1; fi

fail=0
for f in "${all[@]}" "${mine[@]}"; do
    o="$out/$(basename "${f%.c}").o"
    [ -f "$o" ] && continue
    in_mine=0
    for m in "${mine[@]}"; do [ "$m" = "$f" ] && in_mine=1; done
    if [ $in_mine = 1 ]; then
        msg=$(clang "${CFLAGS[@]}" -c "$f" -o "$o" 2>&1)
        rc=$?
        if [ -n "$msg" ]; then echo "$msg"; fi
        if [ $rc != 0 ]; then echo "[check] FAIL compile: $f"; fail=1; elif [ -n "$msg" ]; then echo "[check] warnings: $f"; fail=1; fi
    else
        clang "${CFLAGS[@]}" -w -c "$f" -o "$o" >/dev/null 2>&1 || true
    fi
done

# symbols defined anywhere (c objects + rt.asm exports)
defs="$out/defs.txt"
{
    for o in "$out"/*.o; do llvm-nm --defined-only --extern-only "$o" | awk '$2 ~ /^[TDBRC]$/ {print $3}'; done
    grep -iE '^\s*public\s+' src/rt.asm | awk '{print $2}'
} | sort > "$defs"

for f in "${mine[@]}"; do
    o="$out/$(basename "${f%.c}").o"
    [ -f "$o" ] || continue
    llvm-nm --undefined-only "$o" | awk '{print $NF}' | while read -r s; do
        case "$s" in __imp_*) continue ;; esac
        grep -qxF "$s" "$defs" || echo "[check] FAIL unresolved in $(basename "$f"): $s"
    done
done | tee "$out/unres.txt"
[ -s "$out/unres.txt" ] && fail=1

if [ $full = 1 ]; then
    dups=$(grep -vE '^(\?\?_C@|__real@|__xmm@)' "$defs" | uniq -d)                  # string / constant comdats are shared
    if [ -n "$dups" ]; then echo "[check] FAIL defined twice: $dups"; fail=1; fi
fi

if [ $fail = 0 ]; then echo "[check] ok (${#mine[@]} file(s))"; fi
exit $fail
