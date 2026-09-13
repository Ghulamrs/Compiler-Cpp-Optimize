#!/bin/sh
# The register of open defects: programs this compiler answers differently from
# clang, kept as programs rather than as prose.
#
#   tests/open.sh          run them all and print the split
#   tests/open.sh <name>   run one, and show what it said
#
# **This gates nothing and always exits 0**, the way tests/corpus.sh does, and
# for a reason of its own: every program here is a *known wrong answer*, so a
# suite that failed on them would be red by construction and would stay red
# until the last one was mended. Put one in tests/cases and the three boxes go
# red on a defect nobody introduced today; leave it in nobody's file and it is
# invisible, which is how the tail of docs/audit-2026-09-06.html came to sit
# unworked while four suites were green. This is the third place: visible,
# counted, and not a gate.
#
# Each program carries a <name>.clang beside it holding the oracle's answer -
# `REFUSE` on its own line, or `ACCEPT` and then the exact stdout. A case is
# *closed* when cxx1 agrees with that file, and a closed case does not belong
# here any more: move it to tests/cases with its .expected or .error, where the
# suites will hold it, and delete it from this directory. The summary names the
# closed ones for exactly that reason.
#
# What a program here is not allowed to be: a shape nobody writes, or one whose
# answer depends on elision. Where a defect is a leak or a double destroy,
# count objects with ../cases/lifetime.h and print the ledger rather than a
# constructor trace - C++11 permits elision, so a trace differs legitimately
# and only the balance says anything.
set -e
cd "$(dirname "$0")/.."
CXX1="${CXX1:-./cxx1i.exe}"

# The same CPU limit every other suite compiles under: a parser that loops on
# bad input is a real failure mode here, and these are programs that already
# meet the parser in an unusual place.
cxx1() { ( ulimit -t 10; $CXX1 "$@" < /dev/null ); }
OUT=tests/out-open
rm -rf "$OUT"; mkdir -p "$OUT"

only="$1"
open_count=0; closed=0
for src in tests/open/*.cpp; do
    base=$(basename "$src" .cpp)
    [ -n "$only" ] && [ "$only" != "$base" ] && continue
    want="tests/open/$base.clang"
    if [ ! -f "$want" ]; then
        echo "  $base: no .clang beside it - the oracle's answer is what makes it a case"
        continue
    fi
    verdict=$(head -1 "$want")

    if cxx1 "$src" -o "$OUT/$base.exe" > "$OUT/$base.err" 2>&1; then
        ( ulimit -t 10; "$OUT/$base.exe" ) > "$OUT/$base.out" 2>/dev/null || true
        if [ "$verdict" = "REFUSE" ]; then
            state="OPEN   over-accepts what clang refuses"
        else
            tail -n +2 "$want" > "$OUT/$base.want"
            if diff -q "$OUT/$base.want" "$OUT/$base.out" > /dev/null 2>&1; then
                state="closed"
            else
                state="OPEN   compiles, and prints something else"
            fi
        fi
    else
        if [ "$verdict" = "REFUSE" ]; then
            state="closed"
        else
            state="OPEN   refuses what clang compiles"
        fi
    fi

    if [ "$state" = "closed" ]; then
        closed=$((closed + 1))
        echo "  closed $base - move it to tests/cases and delete it from here"
    else
        open_count=$((open_count + 1))
        printf '  %-34s %s\n' "$base" "$state"
        if [ -n "$only" ]; then
            echo "    clang:"; sed 's/^/      /' "$want"
            echo "    cxx1:"; head -3 "$OUT/$base.err" "$OUT/$base.out" 2>/dev/null | sed 's/^/      /'
        fi
    fi
done

echo "open.sh: $open_count still differ from clang, $closed now agree"
exit 0
