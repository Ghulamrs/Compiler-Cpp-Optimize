#!/bin/sh
# run-linux.sh - the Linux box's half of tools/c6747-compilerpp, run in the directory holding
# harness.cpp, compilerpp_amalgamated.cpp, h.cpp11.obj, C6747-ddr.cmd and runca.js: Compiler++
# built by CCS 5.5's cl6x 7.4.4 at -O2, and cpp11's object, both linked by 7.4.4's linker
# against rts6740_elf_eh.lib and timed on the C6747 cycle-accurate simulator in turn - the box
# has 419 MB, so nothing here runs at once. Writes <build>.result, .cio and .map.
set -u
W=$(cd "$(dirname "$0")" && pwd)
CCS=${CCS55:-$HOME/ti/ccsv5}
CG=$CCS/tools/compiler/c6000_7.4.4
EHLIB=${C6747_EHLIB:-$HOME/c6747-lib}
cd "$W" || exit 1
rm -f ./*.result ./*.cio ./*.out
mkdir -p o55
LINK="-z --rom_model -i$EHLIB C6747-ddr.cmd -lrts6740_elf_eh.lib"
"$CG/bin/cl6x" -mv6740 --abi=eabi -O2 --rtti --symdebug:none -I"$CG/include" -I. --obj_directory=o55 \
    harness.cpp $LINK -m ccs55.map -o ccs55.out > ccs55.build.log 2>&1
"$CG/bin/cl6x" -mv6740 --abi=eabi h.cpp11.obj $LINK -m cpp11.map -o cpp11.out > cpp11.build.log 2>&1
for b in ccs55 cpp11; do
    if [ ! -f $b.out ]; then echo "BUILD linux-$b build=FAILED" | tee $b.result; continue; fi
    # Timed only, and unsampled: a halt costs cycles. The Windows box profiles.
    r=$("$CCS/ccs_base/scripting/bin/dss.sh" "$W/runca.js" "$W/c6747ca-linux.ccxml" "$W/$b.out" "$W/$b.cio" 0 86400000 2>&1 \
        | grep '^RESULT')
    echo "BUILD linux-$b build=ok ${r#RESULT }" | tee $b.result
done
