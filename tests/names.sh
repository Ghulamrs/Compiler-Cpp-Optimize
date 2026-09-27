#!/bin/sh
# Every case's linkage names, against clang's, for all three ABIs.
#
# This is the suite that says the platform ABI was conformed to rather than
# approximated. It needs clang - which can be asked for Microsoft names on any
# machine, with -target x86_64-pc-windows-msvc - and skips itself where there
# is none, saying so rather than passing quietly.
set -e
cd "$(dirname "$0")/.."

if ! command -v clang++ > /dev/null 2>&1; then
    echo "names.sh: skipped - no clang++ to ask"
    exit 0
fi

# Every case at once, JOBS of them (the machine's processors by default), each through
# tools/mangled-names into a report of its own, read back in case order.
OUT=tests/out-names
if [ "${1:-}" = --one ]; then
    if out=$(tools/mangled-names "tests/cases/$2.cpp" 2>&1); then echo pass > "$OUT/$2.verdict"
    else { echo "FAIL $2:"; echo "$out" | sed 's/^/      /'; } > "$OUT/$2.report"; echo fail > "$OUT/$2.verdict"; fi
    exit 0
fi
rm -rf "$OUT"; mkdir -p "$OUT"
JOBS=${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)}
cases() {
    for src in tests/cases/*.cpp; do
        case "$(basename "$src")" in *" "[0-9]*) continue;; esac
        base=$(basename "$src" .cpp)
        [ -f "tests/cases/$base.error" ] && continue
        echo "$base"
    done
}
cases | xargs -P "$JOBS" -n 1 sh "$0" --one
for base in $(cases); do [ -f "$OUT/$base.report" ] && cat "$OUT/$base.report"; done
pass=$(cat "$OUT"/*.verdict 2>/dev/null | grep -c pass || true)
fail=$(cat "$OUT"/*.verdict 2>/dev/null | grep -c fail || true)

echo "names.sh: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
