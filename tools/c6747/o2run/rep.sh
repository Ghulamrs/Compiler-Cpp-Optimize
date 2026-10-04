#!/bin/bash
# rep.sh <windows|linux> - scenario 1: the same images run again and again on the CCS 5.5 simulator,
# on two lanes at once: the test lane runs cpp11 -O2's images REPS times (default 10), the reference
# lane cl6x's REFREPS times (default 2). Builds come from o2run.sh's build phase (plan-build.txt).
set -u
BOX=$1; W=$(cd "$(dirname "$0")" && pwd); cd "$W"
REPS=${REPS:-10}; REFREPS=${REFREPS:-2}
log() { echo "$(date +%H:%M:%S) $*" | tee -a "$W/progress.log"; }
if [ "$BOX" = windows ]; then wp() { cygpath -m "$1"; }; else wp() { echo "$1"; }; fi
PHASES="info examples build" ./o2run.sh "$BOX"
mkdir -p lanes; : > lanes/test.txt; : > lanes/ref.txt
for r in $(seq 1 "$REPS"); do while read -r n tag; do b=$W/b/$n/$tag
    [ "$tag" = cpp11-O2 ] && [ "$(cat "$b/status" 2>/dev/null)" = ok ] &&
        echo "$(wp "$b/image.out") $(wp "$b/sim.r$r.cio") $(wp "$b/sim.r$r.result") 600000" >> lanes/test.txt
done < plan-build.txt; done
for r in $(seq 1 "$REFREPS"); do while read -r n tag; do b=$W/b/$n/$tag
    [ "$tag" != cpp11-O2 ] && [ "$(cat "$b/status" 2>/dev/null)" = ok ] &&
        echo "$(wp "$b/image.out") $(wp "$b/sim.r$r.cio") $(wp "$b/sim.r$r.result") 600000" >> lanes/ref.txt
done < plan-build.txt; done
run_lane() {   # run_lane <list> <name>: the list in chunks of 40, a fresh JVM for each
    rm -rf "lanes/$2"; mkdir -p "lanes/$2"; split -l 40 -d -a 3 "$1" "lanes/$2/c"
    for c in lanes/$2/c*; do
        if [ "$BOX" = windows ]; then
            /c/ti/ccsv5/eclipse/eclipsec.exe -nosplash -data "$(wp "$W/ws/$2")" -application com.ti.ccstudio.apps.runScript \
                -dss.rhinoArgs "$(wp "$W/runbatch.js") $(wp "$W/c6747ca-windows.ccxml") $(wp "$W/$c")" >> "lanes/$2.log" 2>&1
        else "$HOME/ti/ccsv5/ccs_base/scripting/bin/dss.sh" "$W/runbatch.js" "$W/c6747ca-linux.ccxml" "$W/$c" >> "lanes/$2.log" 2>&1; fi
    done
}
log "phase lanes: test $(wc -l < lanes/test.txt) runs, reference $(wc -l < lanes/ref.txt) runs"
run_lane lanes/test.txt test & sleep 15
run_lane lanes/ref.txt ref & wait
log "lanes: $(grep -l 'RESULT failed' b/*/*/sim.r*.result 2>/dev/null | wc -l) runs failed"
log "done"; touch "$W/rep.done"
