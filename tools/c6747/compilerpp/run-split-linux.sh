#!/bin/sh
# run-split-linux.sh - the Linux box's half of the split Compiler++ table: the harness built by
# CCS 5.5's cl6x 7.4.4 -O2 and cpp11's object linked by 7.4.4, then each file timed on its own
# simulator run, stopped at 30 minutes - in turn, never two at once: the box has 419 MB.
# Run in the directory holding harness.cpp, compilerpp_amalgamated.cpp, h.cpp11.obj,
# C6747-ddr.cmd, runca.js and c6747ca-linux.ccxml. Writes <build>.<file>.result and .cio.
# FILES=10 by default; BUILDS="ccs55 cpp11".
set -u
W=$(cd "$(dirname "$0")" && pwd)
CCS=${CCS55:-$HOME/ti/ccsv5}
CG=$CCS/tools/compiler/c6000_7.4.4
EHLIB=${C6747_EHLIB:-$HOME/c6747-lib}
FILES=${FILES:-10}
BUILDS=${BUILDS:-"ccs55 cpp11"}
cd "$W" || exit 1
rm -f ./*.result ./*.cio
mkdir -p o55
LINK="-z --rom_model -i$EHLIB C6747-ddr.cmd -lrts6740_elf_eh.lib"
[ -f ccs55.out ] || "$CG/bin/cl6x" -mv6740 --abi=eabi -O2 ${TI_COMPRESS:-} --rtti --symdebug:none -I"$CG/include" -I. \
    --obj_directory=o55 harness.cpp $LINK -m ccs55.map -o ccs55.out > ccs55.build.log 2>&1
[ -f cpp11.out ] || "$CG/bin/cl6x" -mv6740 --abi=eabi h.cpp11.obj $LINK -m cpp11.map -o cpp11.out > cpp11.build.log 2>&1
k=0
while [ $k -lt "$FILES" ]; do
    for b in $BUILDS; do
        if [ ! -f $b.out ]; then echo "BUILD linux-$b file=$k build=FAILED" | tee $b.$k.result; continue; fi
        r=$("$CCS/ccs_base/scripting/bin/dss.sh" "$W/runca.js" "$W/c6747ca-linux.ccxml" "$W/$b.out" "$W/$b.$k.cio" \
            0 1800000 nocache harnessFile=$k 2>&1 | grep '^RESULT')
        [ -n "$r" ] && r=${r#RESULT } || r=timeout-or-failed
        echo "BUILD linux-$b file=$k build=ok $r" | tee $b.$k.result
    done
    k=$((k + 1))
done
