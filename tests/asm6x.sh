#!/bin/sh
# The tms6747 target through asm6x, the project's own C6000 assembler: every
# case tests/tms6747.sh would compile is taken to a TI object by the driver
# itself (`cpp11 -arch tms6747 -c`, which runs asm6x on what it wrote), and
# that object must be the one asm6x writes for the same assembly by hand -
# every table of it, the file symbol aside, which names the temporary. What
# this checks is the driver's path to the assembler, on any host; that asm6x's
# objects are TI's is ASM6x's own suite, and that TI links them is
# tools/windows/ti-link.cmd on the box, where lnk6x is.
#
#   ASM6X=<asm6x> sh tests/asm6x.sh        the assembler: $ASM6X, else ASM6x's
#                                          build beside this tree, else one on PATH
set -u
cd "$(dirname "$0")/.."
CXX1="${CXX1:-./cpp11.exe}"
ASM6X="${ASM6X:-}"
if [ -z "$ASM6X" ]; then
    for c in ../../ASM6x/build/asm6x.exe "$HOME/asm6x-build/asm6x.exe" "$(command -v asm6x 2>/dev/null)"; do
        [ -n "$c" ] && [ -x "$c" ] && { ASM6X=$c; break; }
    done
fi
[ -n "$ASM6X" ] || { echo "asm6x.sh: no asm6x - name it with ASM6X=<path>"; exit 2; }
C6XDIFF="${C6XDIFF:-../../ASM6x/tests/c6xdiff.py}"
[ -f "$C6XDIFF" ] || C6XDIFF="$HOME/asm6x-build/tests/c6xdiff.py"
[ -f "$C6XDIFF" ] || { echo "asm6x.sh: no c6xdiff.py - name it with C6XDIFF=<path>"; exit 2; }
OUT=tests/out-asm6x
rm -rf "$OUT"; mkdir -p "$OUT"

pass=0; fail=0; skip=0
for src in tests/cases/*.cpp; do
    base=$(basename "$src" .cpp)
    case "$base" in *" "*) continue;; esac
    [ -f "tests/cases/$base.error" ] && continue
    if [ -f "tests/cases/$base.notarget" ] && grep -q "^tms6747[[:space:]]" "tests/cases/$base.notarget"; then
        skip=$((skip + 1)); continue
    fi
    if ! ( ulimit -t 20; CPP11_AS="$ASM6X" "$CXX1" -arch tms6747 -nologo -c "$src" -o "$OUT/$base.obj" < /dev/null ) 2>"$OUT/$base.err"; then
        echo "FAIL $base: the driver did not make an object"
        sed 's/^/      /' "$OUT/$base.err" | head -3
        fail=$((fail + 1)); continue
    fi
    "$CXX1" -arch tms6747 -nologo -S "$src" -o "$OUT/$base.s" < /dev/null 2>/dev/null
    if ! "$ASM6X" "$OUT/$base.s" -o "$OUT/$base.hand.obj" > "$OUT/$base.hand.err" 2>&1; then
        echo "FAIL $base: asm6x refused the assembly by hand"
        sed 's/^/      /' "$OUT/$base.hand.err" | head -3
        fail=$((fail + 1)); continue
    fi
    if python3 "$C6XDIFF" "$OUT/$base.obj" "$OUT/$base.hand.obj" > "$OUT/$base.diff"; then
        pass=$((pass + 1))
    else
        echo "FAIL $base: the driver's object is not asm6x's"
        sed 's/^/      /' "$OUT/$base.diff" | head -6
        fail=$((fail + 1))
    fi
done
echo "asm6x.sh: $pass objects through the driver as by hand, $fail failed, $skip not for this target"
[ "$fail" -eq 0 ]
