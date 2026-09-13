#!/bin/sh
# The 424 C cases this compiler was forked with, run and counted.
#
#   tests/corpus.sh          run them all and print the split
#   tests/corpus.sh <name>   run one, and show what it said
#
# **This gates nothing and always exits 0.** The corpus came over from
# Compiler-C untriaged: it is C90, this is a C++11 compiler, and a case that
# stops compiling here may be a fault or may be C++ doing what C++ does - the
# string-literal rule alone accounts for a pile of them. A number from an
# untriaged corpus is not a pass rate and must not be quoted as one.
#
# What it is for is the *failing set*: run it before a change and after one, and
# diff tests/out-corpus/FAILING. A case that moves between the two lists is the
# thing worth reading, and until this script existed nothing in the tree could
# say which cases those were - the number 379/424 was quoted eleven times in
# CLAUDE.md with no way left to reproduce it.
#
# Each case says what its program should exit with, in a `// expect: N` line.
set -u
cd "$(dirname "$0")/.."
CXX1="${CXX1:-./cxx1i.exe}"
OUT=tests/out-corpus
rm -rf "$OUT"; mkdir -p "$OUT"

only=${1:-}
passed=0; refused=0; wrong=0
: > "$OUT/FAILING"

for src in tests/c-corpus/*.c; do
    base=$(basename "$src" .c)
    [ -n "$only" ] && [ "$only" != "$base" ] && continue

    want=$(sed -n 's|^// *expect: *\([0-9][0-9]*\).*|\1|p' "$src" | head -1)
    if [ -z "$want" ]; then
        echo "  $base: no 'expect:' line" >> "$OUT/FAILING"
        continue
    fi

    if ! ( ulimit -t 10; $CXX1 "$src" -o "$OUT/$base" ) >"$OUT/$base.err" 2>&1; then
        refused=$((refused + 1))
        echo "refused $base: $(head -1 "$OUT/$base.err")" >> "$OUT/FAILING"
        [ -n "$only" ] && { echo "refused:"; sed 's/^/    /' "$OUT/$base.err"; }
        continue
    fi

    ( ulimit -t 10; "$OUT/$base" ) >/dev/null 2>&1
    got=$?
    if [ "$got" = "$want" ]; then
        passed=$((passed + 1))
        [ -n "$only" ] && echo "passed: exit $got, as expected"
    else
        wrong=$((wrong + 1))
        echo "wrong   $base: expected $want, got $got" >> "$OUT/FAILING"
        [ -n "$only" ] && echo "wrong: expected $want, got $got"
    fi
done

[ -n "$only" ] && exit 0
echo "corpus.sh: $passed ran and agreed, $refused refused, $wrong wrong, of 424"
echo "corpus.sh: the failing set is $OUT/FAILING - diff it across a change"
exit 0
