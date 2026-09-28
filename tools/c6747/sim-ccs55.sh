#!/bin/sh
# sim-ccs55.sh <prog.c> ... - the Linux CCS 5.5 box: each program built by CCS 5.5's
# C6000 compiler 7.4.4 and run on the C6747 cycle-accurate simulator through DSS.
# Writes out/<prog>.linux-ccs55.stdout and .result beside this script; prints the result lines.
# CCS 5.5 is 32-bit and runs here on the i686 runtime in /opt/i386 (see CLAUDE.md).
set -u
W=$(cd "$(dirname "$0")" && pwd)
CCS=${CCS55:-$HOME/ti/ccsv5}
CG=$CCS/tools/compiler/c6000_7.4.4
BOX=linux-ccs55
[ -x "$CG/bin/cl6x" ] || { echo "RESULT $BOX - no cl6x at $CG"; exit 1; }
mkdir -p "$W/out"
status=0
for src in "$@"; do
    n=$(basename "$src" .c); o=$W/out/$n.$BOX
    rm -f "$o".*
    if ! "$CG/bin/cl6x" -mv6740 --abi=eabi -g -I"$CG/include" --obj_directory="$W/out" "$src" \
         -z -m "$o.map" --heap_size=0x800 --stack_size=0x800 -i"$CG/lib" --rom_model \
         -o "$o.out" "$W/C6747.cmd" -llibc.a > "$o.build.log" 2>&1; then
        echo "BOX $BOX $n build=FAILED" | tee "$o.result"; status=1; continue
    fi
    r=$("$CCS/ccs_base/scripting/bin/dss.sh" "$W/runca.js" "$W/c6747ca-linux.ccxml" "$o.out" "$o.stdout" 0 \
        2>&1 | grep '^RESULT')
    echo "BOX $BOX $n build=ok ${r#RESULT }" | tee "$o.result"
    [ -n "$r" ] || status=1
done
exit $status
