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
# cases whose expected output was written by a 64-bit-long host.
set -u

cd "$(dirname "$0")/.."
CXX1="${CXX1:-./cpp11.exe}"
VM="${VM:-../Emulator/vm6747.exe}"
OUT=tests/out-tms6747
rm -rf "$OUT"; mkdir -p "$OUT"
if [ ! -x "$VM" ]; then echo "tms6747.sh: no emulator at $VM"; exit 1; fi

pass=0; fail=0; skipEh=0; skipLp=0
only="${1:-}"
for src in tests/cases/*.cpp; do
    base=$(basename "$src" .cpp)
    [ -n "$only" ] && [ "$base" != "$only" ] && continue
    [ -f "tests/cases/$base.error" ] && continue
    if [ -z "$only" ] && grep -q "^$base[[:space:]]" tests/tms6747-exceptions.txt; then skipEh=$((skipEh + 1)); continue; fi
    if [ -z "$only" ] && grep -q "^$base[[:space:]]" tests/tms6747-lp64.txt; then skipLp=$((skipLp + 1)); continue; fi

    if ! ( ulimit -t 10; "$CXX1" -S -arch tms6747 -nologo "$src" -o "$OUT/$base.s" < /dev/null ) 2>"$OUT/$base.err"; then
        echo "FAIL $base: cpp11 refused it"
        sed 's/^/      /' "$OUT/$base.err" | head -3
        fail=$((fail + 1))
        continue
    fi
    { "$VM" "$OUT/$base.s" > "$OUT/$base.out" 2>&1 < /dev/null; } 2>/dev/null || true
    if diff -q "tests/cases/$base.expected" "$OUT/$base.out" >/dev/null; then
        pass=$((pass + 1))
    else
        echo "FAIL $base:"
        diff "tests/cases/$base.expected" "$OUT/$base.out" | sed 's/^/      /' | head -8
        fail=$((fail + 1))
    fi
done

echo "tms6747.sh: $pass passed, $fail failed, $skipEh skipped for exceptions, $skipLp skipped for a 64-bit long"
[ "$fail" -eq 0 ]
