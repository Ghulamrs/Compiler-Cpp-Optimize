#!/bin/sh
# Overload resolution, checked against clang rather than against a recorded
# answer.
#
# **Why this is a suite of its own and not more cases in tests/cases.** A case
# there carries a `.expected` file, which is a decision written down once - and
# for overload resolution the interesting question is not "what does this
# print" but "does cxx1 choose the function clang chooses". Those are the same
# question only while somebody keeps the recorded answer honest. Here the
# oracle is asked on every run, so the corpus cannot drift away from it.
#
# **It compares the verdict before it compares the output**, and that half is
# the one that catches real bugs. Overload resolution is as much about refusing
# an ambiguity as about picking a winner, and a harness that only diffs the
# output of programs that compiled would call "cxx1 accepted what clang calls
# ambiguous" a pass. Every file here is run both ways:
#
#   clang refuses  -> cxx1 must refuse
#   clang accepts  -> cxx1 must accept AND print the same thing
#
# Each program prints a number per call, and each overload returns a number of
# its own, so identical output means identical choices. `0 * argument` keeps
# the parameters used - so a warning about an unused one cannot hide a bug -
# without letting the value affect which number comes out.
#
# clang is needed, so this skips where there is none, exactly as names.sh does.
# The Linux box has no clang and reports the skip rather than a pass.
set -u
cd "$(dirname "$0")/.."
CXX1="${CXX1:-./cpp11.exe}"
CLANG=${CLANG:-clang++}

if ! command -v "$CLANG" > /dev/null 2>&1; then
    echo "overload.sh: skipped - no $CLANG to ask"
    exit 0
fi

OUT=tests/out-overload
# One file: clang and cxx1 side by side - the reference and the compiler under test at once - then
# the verdicts compared. The files run at once too, JOBS of them; each report is read back in order.
one() {
    base=$1; src=tests/overload/$base.cpp
    (
        if "$CLANG" -x c++ -std=c++11 -w "$src" -o "$OUT/$base.clang" \
                    2> "$OUT/$base.clang.err"; then
            echo accept > "$OUT/$base.clang.verdict"
            "$OUT/$base.clang" > "$OUT/$base.clang.out" 2>&1 || true
        else
            echo refuse > "$OUT/$base.clang.verdict"
        fi

    ) &
    (
        if ( ulimit -t 10; $CXX1 "$src" -o "$OUT/$base.cxx1" < /dev/null ) \
               2> "$OUT/$base.cxx1.err"; then
            echo accept > "$OUT/$base.cxx1.verdict"
            "$OUT/$base.cxx1" > "$OUT/$base.cxx1.out" 2>&1 || true
        else
            echo refuse > "$OUT/$base.cxx1.verdict"
        fi

    ) &
    wait
    clangVerdict=$(cat "$OUT/$base.clang.verdict")
    cxx1Verdict=$(cat "$OUT/$base.cxx1.verdict")
    if [ "$clangVerdict" != "$cxx1Verdict" ]; then
        echo "FAIL $base: clang ${clangVerdict}s it and cxx1 ${cxx1Verdict}s it"
        if [ "$cxx1Verdict" = refuse ]; then
            sed 's/^/      /' "$OUT/$base.cxx1.err" | head -4
        else
            sed 's/^/      /' "$OUT/$base.clang.err" | head -4
        fi
        echo fail > "$OUT/$base.result"
        return
    fi

    # Both refused. That they refused for the same *reason* is not asked here
    # - a wording diff is not a resolution bug - and the cases that pin a
    # particular message live in tests/cases with a `.error` beside them.
    if [ "$clangVerdict" = refuse ]; then
        echo pass > "$OUT/$base.result"
        return
    fi

    if diff -q "$OUT/$base.clang.out" "$OUT/$base.cxx1.out" > /dev/null; then
        echo pass > "$OUT/$base.result"
    else
        echo "FAIL $base: both compiled and they chose differently"
        echo "      clang: $(cat "$OUT/$base.clang.out")"
        echo "      cxx1 : $(cat "$OUT/$base.cxx1.out")"
        echo fail > "$OUT/$base.result"
    fi
}
if [ "${1:-}" = --one ]; then one "$2" > "$OUT/$2.report" 2>&1; exit 0; fi
rm -rf "$OUT"; mkdir -p "$OUT"
JOBS=${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)}
for src in tests/overload/*.cpp; do basename "$src" .cpp; done | xargs -P "$JOBS" -n 1 sh "$0" --one
for src in tests/overload/*.cpp; do cat "$OUT/$(basename "$src" .cpp).report"; done
pass=$(cat "$OUT"/*.result 2>/dev/null | grep -c pass || true)
fail=$(cat "$OUT"/*.result 2>/dev/null | grep -c fail || true)

echo "overload.sh: $pass agreed with clang, $fail differed"
[ "$fail" -eq 0 ]
