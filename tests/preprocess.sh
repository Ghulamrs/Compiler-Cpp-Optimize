#!/bin/sh
# cpp11's preprocessor against clang's, the output of phase 4 compared token for token.
#
# Every preprocessor-* case, and every case that writes #define or #if and includes
# no header (a header's text is the host's, not the language's), goes through
# `cpp11 -E` and `clang -E -P`. cpp11's output is put through clang -E -P once more,
# which strips the comments cpp11 leaves for its lexer; then every blank goes, and
# the two are diffed line by line. A case one refuses and the other does not
# differs; both refusing agrees. tests/preprocess-known.txt names a difference that
# is understood, one case a line with its reason, and the reason is printed every run.
#
#     tests/preprocess.sh            CXX1 (./cpp11.exe) and CLANG (clang++) name the two
#
# Skips itself, saying so, where there is no clang or cpp11 has no -E.
cd "$(dirname "$0")/.."
CXX1="${CXX1:-./cpp11.exe}"
CLANG="${CLANG:-clang++}"
OUT=tests/out-preprocess
if ! command -v "$CLANG" >/dev/null 2>&1; then echo "preprocess.sh: skipped - no $CLANG"; exit 0; fi
printf 'int x;\n' > /tmp/cpp11-E-probe.$$.cpp
if ! "$CXX1" -nologo -E /tmp/cpp11-E-probe.$$.cpp >/dev/null 2>&1; then
    rm -f /tmp/cpp11-E-probe.$$.cpp; echo "preprocess.sh: skipped - $CXX1 has no -E"; exit 0
fi
rm -f /tmp/cpp11-E-probe.$$.cpp
rm -rf "$OUT"; mkdir -p "$OUT"

# Spacing between tokens is each preprocessor's own, so a line is compared with none; a
# #pragma is handed on by clang -E and consumed by cpp11, which acts on it in phase 4.
norm() { tr -d '\r' | grep -v '^[[:space:]]*#[[:space:]]*pragma' | sed 's/[[:space:]]//g' | grep -v '^$'; }

list=$( (ls tests/cases/preprocessor-*.cpp; grep -l '^[[:space:]]*#[[:space:]]*\(define\|if\)' tests/cases/*.cpp |
        xargs grep -L '#[[:space:]]*include') 2>/dev/null | sort -u)
agree=0; differ=0; known=0
KNOWN=tests/preprocess-known.txt
for src in $list; do
    base=$(basename "$src" .cpp)
    reason=$(grep "^$base[[:space:]]" "$KNOWN" 2>/dev/null | sed "s/^$base[[:space:]]*//")
    "$CXX1" -nologo -E "$src" > "$OUT/$base.cpp11.raw" 2> "$OUT/$base.cpp11.err"; c=$?
    "$CLANG" -std=c++11 -pedantic-errors -E -P "$src" > "$OUT/$base.clang.raw" 2> "$OUT/$base.clang.err"; k=$?
    if [ $c -ne 0 ] || [ $k -ne 0 ]; then
        if [ $c -ne 0 ] && [ $k -ne 0 ]; then agree=$((agree + 1)); continue; fi
        if [ -n "$reason" ]; then echo "  known $base: $reason"; known=$((known + 1)); continue; fi
        echo "DIFFERS $base: cpp11 exit $c, clang exit $k"; differ=$((differ + 1)); continue
    fi
    "$CLANG" -std=c++11 -E -P -x c++ - < "$OUT/$base.cpp11.raw" 2>/dev/null | norm > "$OUT/$base.cpp11"
    norm < "$OUT/$base.clang.raw" > "$OUT/$base.clang"
    if diff "$OUT/$base.clang" "$OUT/$base.cpp11" > "$OUT/$base.diff"; then
        agree=$((agree + 1))
        [ -n "$reason" ] && { echo "FIXED $base agrees now - remove it from $KNOWN"; differ=$((differ + 1)); }
    elif [ -n "$reason" ]; then echo "  known $base: $reason"; known=$((known + 1))
    else echo "DIFFERS $base:"; sed 's/^/      /' "$OUT/$base.diff" | head -20; differ=$((differ + 1)); fi
done
echo "preprocess.sh: $agree agreed with clang, $known known differences, $differ differed"
[ "$differ" -eq 0 ]
