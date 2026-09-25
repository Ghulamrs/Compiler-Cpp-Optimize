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
rm -rf "$OUT"; mkdir -p "$OUT"

pass=0; fail=0
for src in tests/overload/*.cpp; do
    base=$(basename "$src" .cpp)

    # The oracle first, so that what cxx1 did is always reported against
    # something rather than on its own.
    if "$CLANG" -x c++ -std=c++11 -w "$src" -o "$OUT/$base.clang" \
                2> "$OUT/$base.clang.err"; then
        clangVerdict=accept
        "$OUT/$base.clang" > "$OUT/$base.clang.out" 2>&1 || true
    else
        clangVerdict=refuse
    fi

    if ( ulimit -t 10; $CXX1 "$src" -o "$OUT/$base.cxx1" < /dev/null ) \
           2> "$OUT/$base.cxx1.err"; then
        cxx1Verdict=accept
        "$OUT/$base.cxx1" > "$OUT/$base.cxx1.out" 2>&1 || true
    else
        cxx1Verdict=refuse
    fi

    if [ "$clangVerdict" != "$cxx1Verdict" ]; then
        echo "FAIL $base: clang ${clangVerdict}s it and cxx1 ${cxx1Verdict}s it"
        if [ "$cxx1Verdict" = refuse ]; then
            sed 's/^/      /' "$OUT/$base.cxx1.err" | head -4
        else
            sed 's/^/      /' "$OUT/$base.clang.err" | head -4
        fi
        fail=$((fail + 1))
        continue
    fi

    # Both refused. That they refused for the same *reason* is not asked here
    # - a wording diff is not a resolution bug - and the cases that pin a
    # particular message live in tests/cases with a `.error` beside them.
    if [ "$clangVerdict" = refuse ]; then
        pass=$((pass + 1))
        continue
    fi

    if diff -q "$OUT/$base.clang.out" "$OUT/$base.cxx1.out" > /dev/null; then
        pass=$((pass + 1))
    else
        echo "FAIL $base: both compiled and they chose differently"
        echo "      clang: $(cat "$OUT/$base.clang.out")"
        echo "      cxx1 : $(cat "$OUT/$base.cxx1.out")"
        fail=$((fail + 1))
    fi
done

echo "overload.sh: $pass agreed with clang, $fail differed"
[ "$fail" -eq 0 ]
