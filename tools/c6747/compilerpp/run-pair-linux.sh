#!/bin/sh
# run-pair-linux.sh [file ...] - the Linux box's half of the split Compiler++ table, sized to the
# box: one physical core and 419 MB, so two runs at a time - ccs55 and cpp11 on the same file -
# each its own Eclipse workspace and a JVM held to 192 MB, each stopped at 30 minutes. Memory is
# logged every 30 s to mem.log. Needs ccs55.out and cpp11.out here (run-split-linux.sh builds them).
# Writes <build>.<file>.result and .cio; files 0-9 when none is named.
set -u
W=$(cd "$(dirname "$0")" && pwd)
CCS=${CCS55:-$HOME/ti/ccsv5}
cd "$W" || exit 1
# TI's own JRE, as dss.sh finds it.
for j in "$CCS/ccs_base/eclipse/jre" "$CCS/eclipse/jre"; do
    [ -d "$j" ] && { export JAVA_HOME=$j; export PATH=$j/bin:$PATH; break; }
done
[ $# -gt 0 ] || set -- 0 1 2 3 4 5 6 7 8 9
mkdir -p ws
( while :; do echo "$(date +%T) $(free -m | awk '/Mem:/ {print "avail", $7} /Swap:/ {print "swap", $3}' | tr '\n' ' ')" >> mem.log; sleep 30; done ) &
MON=$!
one() {
    "$CCS/eclipse/eclipse" -nosplash -data "$W/ws/$1.$2" -application com.ti.ccstudio.apps.runScript \
        -dss.rhinoArgs "$W/runca.js $W/c6747ca-linux.ccxml $W/$1.out $W/$1.$2.cio 0 1800000 nocache harnessFile=$2" \
        -vmargs -Xms40m -Xmx192m > "$1.$2.log" 2>&1
    r=$(grep '^RESULT' "$1.$2.log")
    [ -n "$r" ] && r=${r#RESULT } || r=timeout-or-failed
    echo "BUILD linux-$1 file=$2 build=ok $r" > "$1.$2.result"
    cat "$1.$2.result"
}
for k in "$@"; do
    one ccs55 "$k" & a=$!
    sleep 5
    one cpp11 "$k" & b=$!
    wait $a $b
done
kill $MON
