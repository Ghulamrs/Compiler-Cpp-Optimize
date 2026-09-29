#!/bin/sh
# The tms6747 backend, run on the VM6747 emulator against the corpus's own
# expected output: every case in tests/cases that is meant to compile is
# compiled with cpp11 for the C6000, run on vm6747, and its stdout and stderr
# must match tests/cases/<case>.expected, as tests/run.sh requires of the
# host target. The cases that are meant to fail to compile (<case>.error)
# are the front end's and are not run here.
#
# Two lists of cases are skipped by name, each with its reason beside it:
# tests/tms6747-exceptions.txt, the cases that throw or need a landing pad,
# which this target does not unwind yet; and tests/tms6747-lp64.txt, the
# cases whose expected output was written by a 64-bit-long host. A case whose
# .notarget names tms6747 is skipped too, with its reason, as run.sh does for
# the host: it is a construct refused by name for this target.
set -u

cd "$(dirname "$0")/.."
CXX1="${CXX1:-./cpp11.exe}"
# The emulator where the VM6747 checkout keeps it, or beside this tree as it was.
if [ -z "${VM:-}" ]; then
    for v in ../VM6747/Emulator/vm6747.exe ../Emulator/vm6747.exe; do [ -x "$v" ] && { VM=$v; break; }; done
    VM=${VM:-../VM6747/Emulator/vm6747.exe}
fi
# CYCLES=1: each case's cycles from main, as TI's simulator counts cycle.CPU (vm6747 -c), taken
# out of its output before the comparison and kept in <case>.cycles; the total is printed.
CYCLES="${CYCLES:-}"
CXX1_FLAGS="${CXX1_FLAGS:-}"   # -O1 or -O2 runs the corpus through the C6000 optimizer
OUT=tests/out-tms6747
# One case by name, or all of them; a worker is handed the name it was asked for after --one.
if [ "${1:-}" = --one ]; then only=$2; else only="${1:-}"; fi
# One case - compiled for the C6000 and run on vm6747 - its report and verdict written beside its
# output, so that the cases run at once: JOBS of them, the machine's processors by default.
one() {
    base=$1; src=tests/cases/$base.cpp
    if [ -f "tests/cases/$base.notarget" ] && grep -q "^tms6747[[:space:]]" "tests/cases/$base.notarget"; then
        echo "  skip $base for tms6747: $(grep "^tms6747[[:space:]]" "tests/cases/$base.notarget" | sed 's/^tms6747[[:space:]]*//')"
        echo notarget > "$OUT/$base.verdict"; return
    fi
    if [ -z "$only" ] && grep -q "^$base[[:space:]]" tests/tms6747-exceptions.txt; then echo eh > "$OUT/$base.verdict"; return; fi
    if [ -z "$only" ] && grep -q "^$base[[:space:]]" tests/tms6747-lp64.txt; then echo lp64 > "$OUT/$base.verdict"; return; fi

    if ! ( ulimit -t 10; "$CXX1" -S -arch tms6747 -nologo $CXX1_FLAGS "$src" -o "$OUT/$base.s" < /dev/null ) 2>"$OUT/$base.err"; then
        echo "FAIL $base: cpp11 refused it"
        sed 's/^/      /' "$OUT/$base.err" | head -3
        echo fail > "$OUT/$base.verdict"
        return
    fi
    if [ -n "$CYCLES" ]; then
        { "$VM" -c "$OUT/$base.s" > "$OUT/$base.raw" 2>&1 < /dev/null; } 2>/dev/null || true
        # The program's last line may have no newline, so the count is cut out exactly as written.
        grep -o 'CYCLES count=[0-9]* packets=[0-9]* natives=[0-9]*' "$OUT/$base.raw" | sed 's/^CYCLES //' > "$OUT/$base.cycles"
        perl -0pe 's/CYCLES count=\d+ packets=\d+ natives=\d+\n//' "$OUT/$base.raw" > "$OUT/$base.out"
    else
        { "$VM" "$OUT/$base.s" > "$OUT/$base.out" 2>&1 < /dev/null; } 2>/dev/null || true
    fi
    if diff -q "tests/cases/$base.expected" "$OUT/$base.out" >/dev/null; then
        echo pass > "$OUT/$base.verdict"
    else
        echo "FAIL $base:"
        diff "tests/cases/$base.expected" "$OUT/$base.out" | sed 's/^/      /' | head -8
        echo fail > "$OUT/$base.verdict"
    fi
}
if [ "${1:-}" = --one ]; then one "$3" > "$OUT/$3.report" 2>&1; exit 0; fi
rm -rf "$OUT"; mkdir -p "$OUT"
if [ ! -x "$VM" ]; then echo "tms6747.sh: no emulator at $VM"; exit 1; fi
cases() {
    for src in tests/cases/*.cpp; do
        base=$(basename "$src" .cpp)
        [ -n "$only" ] && [ "$base" != "$only" ] && continue
        [ -f "tests/cases/$base.error" ] && continue
        echo "$base"
    done
}
JOBS=${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)}
cases | xargs -P "$JOBS" -I{} sh "$0" --one "$only" {}
for base in $(cases); do cat "$OUT/$base.report"; done
count() { cat "$OUT"/*.verdict 2>/dev/null | grep -cx "$1" || true; }
pass=$(count pass); fail=$(count fail); skipEh=$(count eh); skipLp=$(count lp64); skipNt=$(count notarget)

echo "tms6747.sh: $pass passed, $fail failed, $skipEh skipped for exceptions, $skipLp skipped for a 64-bit long, $skipNt not for this target"
if [ -n "$CYCLES" ]; then
    for base in $(cases); do [ -s "$OUT/$base.cycles" ] && echo "$base $(cat "$OUT/$base.cycles")"; done > "$OUT/cycles.txt"
    awk '{ sub("count=", "", $2); s += $2 } END { printf "tms6747.sh: %d cycles (cycle.CPU) over %d cases, in %s\n", s, NR, FILENAME }' "$OUT/cycles.txt"
fi
[ "$fail" -eq 0 ]
