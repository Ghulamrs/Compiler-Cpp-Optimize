#!/bin/sh
# **Two compilers, the same assembly, byte for byte.** The gate for a change
# that is meant to reorganize the optimizer without changing what it emits:
# every case in tests/cases that compiles, and every unit of Compiler++,
# through both compilers at -O0, -O1 and -O2 for x86_64-windows in the GNU and
# the MASM spellings, and at -O1 and -O2 for x86_64-linux; the outputs diffed,
# the diagnostics and exit status with them.
#
# Usage: tools/identical.sh REF NEW [CPP] [OUT]
#   REF  the reference cxx1.exe (a build of the untouched base)
#   NEW  the cxx1.exe under test
#   CPP  a Compiler++ tree (its units are CPP/Compiler++/*.cpp); default
#        ~/cxxopt-build/cpp, skipped if it is not there
#   OUT  a scratch directory; default ~/cxxopt-build/identical
#
# Prints the number of outputs compared and the number that differ, and names
# each one that does; exits 1 if any does. Runs the compiles eight at a time,
# each under a CPU limit, and the whole thing under a thirty-minute alarm.
#
# Called with one argument, `--one`, it is its own helper: one job, both sides.
set -u
if [ "${1:-}" = --one ]; then
    # --one REF NEW OUT NAME SRC FLAGS...
    REF=$2 NEW=$3 OUT=$4 NAME=$5 SRC=$6; shift 6
    # A diagnostic names the compiler by its own path, which differs between
    # the two sides by construction; that name is made the same before the diff.
    ( ulimit -t 60; "$REF" "$@" "$SRC" -o "$OUT/$NAME.ref.s" < /dev/null 2>&1; echo "exit=$?" ) | sed "s|$REF|cxx1|g" > "$OUT/$NAME.ref.err"
    ( ulimit -t 60; "$NEW" "$@" "$SRC" -o "$OUT/$NAME.new.s" < /dev/null 2>&1; echo "exit=$?" ) | sed "s|$NEW|cxx1|g" > "$OUT/$NAME.new.err"
    exit 0
fi

cd "$(dirname "$0")/.."
SELF=$(pwd)/tools/identical.sh
REF=$1 NEW=$2 CPP=${3:-$HOME/cxxopt-build/cpp} OUT=${4:-$HOME/cxxopt-build/identical}
case $REF in /*) ;; *) REF=$(pwd)/$REF ;; esac
case $NEW in /*) ;; *) NEW=$(pwd)/$NEW ;; esac
rm -rf "$OUT"; mkdir -p "$OUT"

# One line per job: the output's name, the source, then the flags - the shape
# xargs -L 1 hands the helper as arguments.
jobs="$OUT/jobs"
: > "$jobs"
sources() {
    for src in tests/cases/*.cpp; do
        base=$(basename "$src" .cpp)
        [ -f "tests/cases/$base.error" ] && continue
        echo "$src"
    done
    [ -d "$CPP/Compiler++" ] && ls "$CPP"/Compiler++/*.cpp
}
for src in $(sources); do
    n=$(echo "$src" | tr '/' '_' | sed 's/\.cpp$//')
    for L in 0 1 2; do
        echo "$n.win.gnu.O$L $src -nologo -arch x86_64-windows -masm=gnu -O$L -S" >> "$jobs"
        echo "$n.win.masm.O$L $src -nologo -arch x86_64-windows -masm=masm -O$L -S" >> "$jobs"
    done
    for L in 1 2; do
        echo "$n.linux.O$L $src -nologo -arch x86_64-linux -O$L -S" >> "$jobs"
    done
done

perl -e 'alarm 1800; exec @ARGV' xargs -P 8 -L 1 sh "$SELF" --one "$REF" "$NEW" "$OUT" < "$jobs"

total=0; differ=0
while read -r name src flags; do
    total=$((total + 1))
    same=1
    # A compile refused on both sides leaves no assembly on either: equal.
    for side in s err; do
        r="$OUT/$name.ref.$side"; n="$OUT/$name.new.$side"
        if [ -f "$r" ] || [ -f "$n" ]; then cmp -s "$r" "$n" 2>/dev/null || same=0; fi
    done
    if [ $same = 0 ]; then
        differ=$((differ + 1))
        echo "DIFFERS $name ($src $flags)"
    fi
done < "$jobs"
echo "identical: $total compared, $differ differ"
[ $differ = 0 ]
