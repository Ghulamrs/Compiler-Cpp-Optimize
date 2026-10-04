#!/bin/bash
# native.sh <windows|linux> - scenario 2: cpp11 -O2 native (Windows: RIDE's masm.exe and link.exe; Linux:
# the system assembler and linker) against the native compiler at -O2 (cl /O2; g++ -O2). Each run times
# bench-kernels (six kernels, ms each, a checksum) and Compiler++ compiling the ten workload files, its
# output compared with the reference build's. Runs: R, T x5, R, T x5 - REF reference and TEST test runs.
set -u
BOX=$1; W=$(cd "$(dirname "$0")" && pwd); N=$W/native; mkdir -p "$N"; cd "$N"
log() { echo "$(date +%H:%M:%S) $*" | tee -a "$W/progress.log"; }
if [ "$BOX" = windows ]; then RIDE="/c/Program Files/RIDE 4.7"; EXE=.exe; else RIDE=/opt/ride-4.7; EXE=; fi
CPP11=$RIDE/bin/cpp11.exe; CPPSRC=$RIDE/projects/compilerpp
SRCS=$(cd "$CPPSRC" && ls *.cpp | grep -v '^sample.cpp$')
log "native: build"
t0=$(date +%s)
"$CPP11" -O2 "$W/bench-kernels.cpp" -o "t-bench$EXE" > build-t-bench.log 2>&1 || log "native: cpp11 bench FAILED"
( cd "$CPPSRC" && "$CPP11" -O2 $SRCS -o "$N/t-cpp$EXE" ) > build-t-cpp.log 2>&1 || log "native: cpp11 compilerpp FAILED"
t1=$(date +%s)
if [ "$BOX" = windows ]; then
    cl -nologo -O2 -EHsc -GR -std:c++14 "$(cygpath -w "$W/bench-kernels.cpp")" -Fe:r-bench.exe -Fo:r-bench.obj > build-r-bench.log 2>&1 || log "native: cl bench FAILED"
    mkdir -p robj; ( cd "$CPPSRC" && cl -nologo -O2 -EHsc -GR -std:c++14 $SRCS "-Fe:$(cygpath -w "$N/r-cpp.exe")" "-Fo:$(cygpath -w "$N/robj")\\" ) > build-r-cpp.log 2>&1 || log "native: cl compilerpp FAILED"
else
    g++ -O2 -std=c++11 -w "$W/bench-kernels.cpp" -o r-bench > build-r-bench.log 2>&1 || log "native: g++ bench FAILED"
    ( cd "$CPPSRC" && g++ -O2 -std=c++11 -w $SRCS -o "$N/r-cpp" ) > build-r-cpp.log 2>&1 || log "native: g++ compilerpp FAILED"
fi
t2=$(date +%s); log "native: built - cpp11 $((t1 - t0)) s, native $((t2 - t1)) s"
one() {   # one <t|r> <run number>: bench-kernels, then Compiler++ over the ten files
    local k=$1 i=$2 o=run.$1.$2 s e
    ./$k-bench$EXE > $o.bench 2>&1; echo "rc=$?" >> $o.bench
    s=$(date +%s%N)
    for f in "$W"/workload/*.cpp; do ./$k-cpp$EXE "$(if [ "$BOX" = windows ]; then cygpath -m "$f"; else echo "$f"; fi)"; echo "rc=$?"; done > $o.cpp 2>&1
    e=$(date +%s%N); echo $(( (e - s) / 1000000 )) > $o.cppms
}
log "native: runs"
one r 1; for i in 1 2 3 4 5; do one t $i; done; one r 2; for i in 6 7 8 9 10; do one t $i; done
log "native: done"; touch "$W/native.done"
