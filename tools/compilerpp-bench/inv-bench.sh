#!/bin/sh
# Compiler++ built by the reference at -O2 and by RIDE's cpp11 -O2, the two builds at once; both
# checked on inverse.cpp; then 1000 compiles with each, 3 rounds, the order alternating.
#   inv-bench.sh <Compiler++ source dir> <reference c++> <cpp11>
SRC=$1; REF=$2; RIDE=$3; N=${N:-2000}; ROUNDS=${ROUNDS:-3}
rm -f rounds.txt fails.txt ref.cxb ride.cxb x.cxb ref.out ride.out
# Run from a scratch directory: the program comes from beside this script, and nothing lands here.
[ -f inverse.cpp ] || cp "$(dirname "$0")/inverse.cpp" . || exit 2
now() { t=$(date +%s%N 2>/dev/null); case $t in *N) python3 -c 'import time;print(int(time.time()*1e9))';; *) echo $t;; esac; }
ms() { echo $(( ($2 - $1) / 1000000 )); }
echo "host: $(uname -sm), $(getconf _NPROCESSORS_ONLN) cores; reference: $($REF --version 2>&1 | head -1)"
( b=$(now); $REF -std=c++11 -O2 -w $SRC/*.cpp -o cpp_ref > ref.build 2>&1; echo "$? $(ms $b $(now))" > ref.rc ) &
( b=$(now); $RIDE -O2 $SRC/*.cpp -o cpp_ride > ride.build 2>&1; echo "$? $(ms $b $(now))" > ride.rc ) &
wait
echo "build (rc ms): reference $(cat ref.rc), cpp11 $(cat ride.rc)"
echo "size: reference $(wc -c < cpp_ref | tr -d ' ') bytes, cpp11 $(wc -c < cpp_ride | tr -d ' ') bytes"
./cpp_ref -q -o ref.cxb inverse.cpp; r1=$?; ./cpp_ride -q -o ride.cxb inverse.cpp; r2=$?
cmp -s ref.cxb ride.cxb && same=identical || same=DIFFERENT
echo "compile: rc $r1 / $r2, bytecode $same ($(wc -c < ref.cxb | tr -d ' ') bytes)"
./cpp_ref -run inverse.cpp > ref.out 2>&1; ./cpp_ride -run inverse.cpp > ride.out 2>&1
cmp -s ref.out ride.out && echo "run: identical - $(sed -n 2,3p ref.out | tr '\n' ' ')" || echo "run: DIFFERENT"
# N compiles in a row; one that fails is counted, so a fast failure cannot pass for a fast compile.
batch() { b=$(now); i=0; f=0; while [ $i -lt $N ]; do ./$1 -q -o x.cxb inverse.cpp || f=$((f+1)); i=$((i+1)); done; e=$(now); echo "$1 $f" >> fails.txt; ms $b $e; }
r=1
while [ $r -le $ROUNDS ]; do
    if [ $((r % 2)) = 1 ]; then a=$(batch cpp_ride); b=$(batch cpp_ref); else b=$(batch cpp_ref); a=$(batch cpp_ride); fi
    echo "round $r: $N compiles - cpp11 $a ms, reference $b ms"; echo "$a $b" >> rounds.txt
    r=$((r+1))
done
mid=$(( (ROUNDS + 1) / 2 ))
echo "median: cpp11 $(cut -d' ' -f1 rounds.txt | sort -n | sed -n ${mid}p) ms, reference $(cut -d' ' -f2 rounds.txt | sort -n | sed -n ${mid}p) ms"
echo "failed compiles: $(awk '{s+=$2} END {print s+0}' fails.txt) of $((N * ROUNDS * 2))"
