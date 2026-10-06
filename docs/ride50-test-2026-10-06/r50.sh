#!/bin/sh
# r50.sh - RIDE 5.0 as installed on the Linux box, against TI (CCS 5.5, cl6x 7.4.4).
# Every program of corpus/ built three ways - c11-O2 c11-Os (installed cpp11 -> asm6x -> lnk6x,
# TI's EH runtime) and ti744-O2 (cl6x 7.4.4 with its own linker) - every image on the installed
# vm6747sim, then on CCS 5.5's C6747 cycle-accurate simulator one session at a time (this box's
# rule) until DEADLINE (epoch seconds), c11-O2 and ti744-O2 first, interleaved; resumable.
#   sh r50.sh [build|sim|ccs|report|all]
set -u
W=$(cd "$(dirname "$0")" && pwd)
PH=${1:-all}
BIN=${BIN:-/usr/local/bin}
CCS=$HOME/ti/ccsv5; CG=$CCS/tools/compiler/c6000_7.4.4; LIB=$HOME/c6747-lib
DEADLINE=${DEADLINE:-0}
VARIANTS="c11-O2 c11-Os ti744-O2"
I=$W/img; S=$W/src; mkdir -p $I $S $I/o-ti744-O2
progs() { cut -d' ' -f1,2 $W/list.txt; }

if [ $PH = build ] || [ $PH = all ]; then
  cp $W/ti-link.cmd $I/
  progs | while read n e; do
    src=$W/corpus/$n.$e; c11=$src
    [ $e = c ] && { cp $src $S/$n.cpp; c11=$S/$n.cpp; }
    for L in O2 Os; do o=$I/$n.c11-$L
      ( $BIN/cpp11 -arch tms6747 -nologo -$L -S $c11 -o $o.s && $BIN/asm6x $o.s -o $o.obj &&
        $BIN/lnk6x -mv6740 --abi=eabi -i $LIB $I/ti-link.cmd $o.obj -l rts6740_elf_eh.lib -o $o.out ) > $o.build.log 2>&1 || rm -f $o.out
    done
    o=$I/$n.ti744-O2; lang=; [ $e = c ] || lang="--exceptions --rtti"
    ( $CG/bin/cl6x -mv6740 --abi=eabi -O2 $lang --symdebug:none -I$CG/include -I$W/corpus --obj_directory=$I/o-ti744-O2 -c $src &&
      $CG/bin/cl6x -mv6740 --abi=eabi -z -i $LIB -i $CG/lib $I/ti-link.cmd $I/o-ti744-O2/$n.obj -l rts6740_elf_eh.lib -o $o.out ) > $o.build.log 2>&1 || rm -f $o.out
  done
  echo "build: $(ls $I/*.out | wc -l) images"
fi

if [ $PH = sim ] || [ $PH = all ]; then
  for o in $I/*.out; do b=${o%.out}
    timeout 600 $BIN/vm6747sim --run -c $o > $b.sim.txt 2> $b.sim.err < /dev/null || [ $? = 124 ] && echo TIMEOUT >> $b.sim.err
  done
  echo "sim: done"
fi

if [ $PH = ccs ] || [ $PH = all ]; then
  for v in c11-O2 ti744-O2 c11-Os; do progs | while read n e; do echo "$n.$v"; done; done |
  awk '{ a[NR]=$0 } END { h=int(NR/3); for (i=1;i<=h;i++) { print a[i]; print a[i+h] } for (i=2*h+1;i<=NR;i++) print a[i] }' |
  while read b; do
    [ "$DEADLINE" -gt 0 ] && [ $(date +%s) -ge "$DEADLINE" ] && { echo "ccs: deadline reached"; break; }
    [ -f $I/$b.out ] || continue
    grep -q '^RESULT' $I/$b.ccs.log 2>/dev/null && continue
    timeout 1800 $CCS/ccs_base/scripting/bin/dss.sh $W/runca.js $W/c6747ca-linux.ccxml $I/$b.out $I/$b.ccs.cio 1 1800000 > $I/$b.ccs.log 2>&1
  done
  echo "ccs: $(grep -l '^RESULT' $I/*.ccs.log | wc -l) runs"
fi

if [ $PH = report ] || [ $PH = all ]; then
  python3 $W/report.py $W linux "$VARIANTS"
fi
