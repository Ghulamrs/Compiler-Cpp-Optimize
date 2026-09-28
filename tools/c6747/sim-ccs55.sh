#!/bin/sh
# sim-ccs55.sh <prog.c> ... - the Linux CCS 5.5 box: each program built by CCS 5.5's
# C6000 compiler 7.4.4 at -O2, and cpp11's object for it when programs/<prog>.cpp11.obj
# is there, each linked by 7.4.4's linker against the same library and run on the C6747
# cycle-accurate simulator through DSS. Writes out/<prog>.<box>.stdout and .result
# beside this script; prints the result lines.
# CCS 5.5 is 32-bit and runs here on the i686 runtime in /opt/i386 (see CLAUDE.md).
set -u
W=$(cd "$(dirname "$0")" && pwd)
CCS=${CCS55:-$HOME/ti/ccsv5}
CG=$CCS/tools/compiler/c6000_7.4.4
EHLIB=${C6747_EHLIB:-$HOME/c6747-lib}   # rts6740_elf_eh.lib, made once by 7.4.4's mklib
BOX=linux-ccs55
[ -x "$CG/bin/cl6x" ] || { echo "RESULT $BOX - no cl6x at $CG"; exit 1; }
[ -f "$EHLIB/rts6740_elf_eh.lib" ] || { echo "RESULT $BOX - no rts6740_elf_eh.lib in $EHLIB"; exit 1; }
mkdir -p "$W/out"
status=0
LINK="-z --heap_size=0x800 --stack_size=0x800 -i$EHLIB --rom_model $W/C6747.cmd -lrts6740_elf_eh.lib"
run() {   # run <prog> <box> <.out> : the simulator, and the result line
    r=$("$CCS/ccs_base/scripting/bin/dss.sh" "$W/runca.js" "$W/c6747ca-linux.ccxml" "$3" "$W/out/$1.$2.stdout" 0 \
        2>&1 | grep '^RESULT')
    echo "BOX $2 $1 build=ok ${r#RESULT }" | tee "$W/out/$1.$2.result"
    [ -n "$r" ] || status=1
}
for src in "$@"; do
    n=$(basename "$src" .c); o=$W/out/$n.$BOX
    rm -f "$W/out/$n.$BOX".*
    if "$CG/bin/cl6x" -mv6740 --abi=eabi -O2 --symdebug:none -I"$CG/include" --obj_directory="$W/out" "$src" \
         $LINK -m "$o.map" -o "$o.out" > "$o.build.log" 2>&1; then
        run "$n" "$BOX" "$o.out"
    else
        echo "BOX $BOX $n build=FAILED" | tee "$o.result"; status=1
    fi
    obj=$(dirname "$src")/$n.cpp11.obj; c=$W/out/$n.$BOX-cpp11
    [ -f "$obj" ] || continue
    rm -f "$c".*
    if "$CG/bin/cl6x" -mv6740 --abi=eabi "$obj" $LINK -m "$c.map" -o "$c.out" > "$c.build.log" 2>&1; then
        run "$n" "$BOX-cpp11" "$c.out"
    else
        echo "BOX $BOX-cpp11 $n build=FAILED" | tee "$c.result"; status=1
    fi
done
exit $status
