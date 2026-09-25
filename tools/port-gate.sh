#!/bin/sh
# The check every Compiler-Cppi port step passed: build, the four suites, the
# tms6747 cases on the VM6747 emulator, the comment cap, output identity against
# a reference build, and the benchmark's assembly against the same reference.
#   REF=path/to/cxx1-reference.exe VM=path/to/vm6747.exe tools/port-gate.sh
# REF was a build of 18d2236 (S13, before the port). An output that differs is
# printed as "new" when REF refuses the case (a case the port added) and as
# "REAL CHANGE" otherwise; each real change has to be explained in the commit.
cd "$(dirname "$0")/.." || exit 1
: "${REF:?set REF to the reference cxx1}"
make -j4 2>&1 | grep -E "error:|undefined" | head
bash tests/run.sh 2>&1 | grep -E "^FAIL|run.sh:" | tail -8
bash tests/names.sh 2>&1 | tail -1
bash tests/overload.sh 2>&1 | tail -1
[ -n "$VM" ] && VM="$VM" bash tests/tms6747.sh 2>&1 | grep -E "^FAIL|tms6747.sh:"
make comments 2>&1 | grep "over the cap"
out=$(mktemp)
LEVELS="0 1 2" sh tools/identical.sh "$REF" ./cxx1.exe > "$out" 2>&1
tail -1 "$out"
for c in $(grep DIFFERS "$out" | sed 's/DIFFERS tests_cases_//; s/\..*//' | sort -u); do
    if "$REF" -S -arch x86_64-linux "tests/cases/$c.cpp" -o /dev/null >/dev/null 2>&1
    then echo "  REAL CHANGE: $c"; else echo "  new: $c"; fi
done
S=tools/windows/bench-kernels.cpp
"$REF" -nologo -S -O2 $S -o "$out.a"; ./cxx1.exe -nologo -S -O2 $S -o "$out.b"
echo "benchmark assembly lines differing from REF: $(diff "$out.a" "$out.b" | grep -c '^[<>]')"
rm -f "$out" "$out.a" "$out.b"
