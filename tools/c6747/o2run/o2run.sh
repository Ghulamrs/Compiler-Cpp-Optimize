#!/bin/bash
# o2run.sh <windows|linux> - one routine -O2 exercise on a C6747 box, from the plan tools/c6747/o2run.py
# wrote beside it: plan-build.txt ("<program> <build>" per line, build one of cpp11-O2 cpp11-O1 744 822)
# and plan-sim.txt ("<program> <build> [harnessFile=<k>]"). The tools are the installed RIDE 4.7's and
# the box's cl6x; every image runs on the CCS 5.5 C6747 cycle-accurate simulator, cpp11's also on VM6747.
# Writes b/<program>/<build>/, timings in progress.log, and o2run.done at the end.
set -u
BOX=$1
W=$(cd "$(dirname "$0")" && pwd)
cd "$W"
if [ "$BOX" = windows ]; then
    export MSYS_NO_PATHCONV=1
    RIDE="/c/Program Files/RIDE 4.7"
    CG744=/c/ti/ccsv5/tools/compiler/c6000_7.4.4; CG822=/c/ti/ccsv7/tools/compiler/ti-cgt-c6000_8.2.2
    LIB744=/c/cxx1/c6747-lib; LIB822=/c/Users/GRA/Documents/VM6747/tilib
    JOBS=${JOBS:-20}; LANES=${LANES:-12}; STAGGER=${STAGGER:-0}; CHUNK=${CHUNK:-80}; HOLD=${HOLD:-8}; DEADLINE=${DEADLINE:-1500}; CCXML=c6747ca-windows.ccxml
    wp() { cygpath -m "$1"; }
else
    RIDE=/opt/ride-4.7
    CG744=$HOME/ti/ccsv5/tools/compiler/c6000_7.4.4; LIB744=$HOME/c6747-lib; CG822=; LIB822=
    JOBS=${JOBS:-2}; LANES=${LANES:-2}; STAGGER=${STAGGER:-0}; CHUNK=${CHUNK:-40}; HOLD=${HOLD:-15}; DEADLINE=${DEADLINE:-1500}; CCXML=c6747ca-linux.ccxml
    wp() { echo "$1"; }
fi
BIN=$RIDE/bin; CPP11=$BIN/cpp11.exe; ASM6X=$BIN/asm6x.exe; LNK6X=$BIN/lnk6x.exe; VM=$BIN/vm6747.exe
log() { echo "$(date +%H:%M:%S) $*" | tee -a "$W/progress.log"; }
meta() { sed -n "s/^$2=//p" "$W/progs/$1/meta"; }

info() {
    { echo "box $BOX $(hostname) $(date)"
      for t in "$CPP11" "$ASM6X" "$LNK6X" "$VM"; do echo "== $t"; md5sum "$t" | cut -c1-32
          timeout 20 "$t" --version < /dev/null 2>&1 | head -3; done
      echo "== cl6x 7.4.4"; "$CG744/bin/cl6x" -version 2>&1 | head -1
      [ -n "$CG822" ] && { echo "== cl6x 8.2.2"; "$CG822/bin/cl6x" -version 2>&1 | head -1; }
    } > "$W/info.txt" 2>&1
}

# The RIDE examples, taken from the RIDE 4.7 installed on this box.
examples() {
    mk() { local n=$1 from=$2; shift 2; local d=$W/progs/$n s=""; rm -rf "$d"; mkdir -p "$d"
        cp "$from"/*.h "$d"/ 2>/dev/null
        for f in "$@"; do cp "$from/$f" "$d/"; s="$s $f"; done
        printf 'SET=examples\nMAP=ddr\nTMO=600000\nSRCS=%s\n' "${s# }" > "$d/meta"
        cp "$W/expected-ex/$n.txt" "$d/expected"; }
    mk ex-sample "$RIDE/projects/ccs/Sample" Math.cpp
    mk ex-sampleext "$RIDE/projects/ccs/SampleExt" Math.cpp
    mk ex-hello-ccs "$RIDE/projects/ccs/Hello" hello.c
    for p in inventory shapes table vector3; do mk ex-cpp-$p "$RIDE/projects/cpp-$p" $(cd "$RIDE/projects/cpp-$p" && ls *.cpp); done
    for p in hello smart words; do mk ex-prog-$p "$RIDE/programs" $p.cpp; done
    mk ex-compilerpp "$RIDE/projects/compilerpp" $(cd "$RIDE/projects/compilerpp" && ls *.cpp | grep -v '^sample.cpp$')
    cp "$RIDE/projects/compilerpp/sample.cpp" "$W/progs/ex-compilerpp/input-sample.cpp"
    echo "ARGS=$(wp "$W/progs/ex-compilerpp/input-sample.cpp")" >> "$W/progs/ex-compilerpp/meta"
}

# One program, one build: the image for the simulator, and for cpp11 the assembly VM6747 runs.
build1() {
    local n=$1 tag=$2 p=$W/progs/$1 b=$W/b/$1/$2
    rm -rf "$b"; mkdir -p "$b"; cd "$b"
    local srcs map heap="" args="" t0; srcs=$(meta "$n" SRCS); t0=$(date +%s.%N)
    if [ "$(meta "$n" MAP)" = shram ]; then map=$W/C6747.cmd; heap="--heap_size=0x800 --stack_size=0x800"
    else map=$W/C6747-ddr.cmd; fi
    [ -n "$(meta "$n" ARGS)" ] && args="--args=1024"
    case $tag in
    cpp11-*)
        local lvl=${tag#cpp11-} objs="" s c
        for s in $srcs; do
            c=${s%.*}; cp "$p/$s" "src.$c.cpp"
            timeout 600 "$CPP11" -arch tms6747 -nologo -$lvl -S -I"$(wp "$p")" "src.$c.cpp" -o "$c.s" >> build.log 2>&1 \
                || { echo compile-failed > status; return; }
            timeout 300 "$ASM6X" "$c.s" -o "$c.obj" >> build.log 2>&1 || { echo asm-failed > status; return; }
            objs="$objs $c.obj"
        done
        echo "$(date +%s.%N) $t0" | awk '{printf "%.2f\n", $1-$2}' > compile.sec
        "$LNK6X" --cgt=7.4.4 -mv6740 --abi=eabi $objs $heap $args -i"$(wp "$LIB744")" --rom_model "$(wp "$map")" \
            -lrts6740_elf_eh.lib -m image.map -o image.out >> build.log 2>&1 || { echo link-failed > status; return; } ;;
    744|822)
        local cg lib s list=""; if [ "$tag" = 744 ]; then cg=$CG744; lib=$LIB744; else cg=$CG822; lib=$LIB822; fi
        local flags="-mv6740 --abi=eabi -O2 --symdebug:none -I$(wp "$cg/include") -I$(wp "$p")"
        case "$srcs" in *.cpp*) flags="$flags --rtti"
            grep -qE '\b(throw|try)\b' $(for s in $srcs; do echo "$p/$s"; done) && flags="$flags --exceptions";; esac
        for s in $srcs; do list="$list $(wp "$p/$s")"; done
        mkdir -p obj
        timeout 7200 "$cg/bin/cl6x" $flags --obj_directory=obj $list > build.log 2>&1 || { echo compile-failed > status; return; }
        echo "$(date +%s.%N) $t0" | awk '{printf "%.2f\n", $1-$2}' > compile.sec
        "$cg/bin/cl6x" -mv6740 --abi=eabi obj/*.obj -z $heap $args --rom_model -i"$(wp "$lib")" "$(wp "$map")" \
            -lrts6740_elf_eh.lib -m image.map -o image.out >> build.log 2>&1 || { echo link-failed > status; return; } ;;
    esac
    echo ok > status
}

# VM6747: cycle.CPU from main, and what the program printed. cpp11 builds only: it cannot run cl6x -O2 code.
vm1() {
    local n=$1 tag=$2 b=$W/b/$1/$2 a; cd "$b" || return
    [ "$(cat status 2>/dev/null)" = ok ] || return
    a=$(meta "$n" ARGS)
    if [ -n "$a" ]; then timeout 900 "$VM" -c *.s -- "$a" < /dev/null > vm.raw 2>&1
    else timeout 900 "$VM" -c *.s < /dev/null > vm.raw 2>&1; fi
    echo $? > vm.rc
    grep -o 'CYCLES count=[0-9]* packets=[0-9]* natives=[0-9]*' vm.raw > vm.cycles
    perl -0pe 's/CYCLES count=\d+ packets=\d+ natives=\d+\r?\n?//' vm.raw > vm.txt
}

# One lane: its list in chunks of CHUNK runs, a fresh simulator JVM for each chunk. One JVM starts at a
# time on the box: a lane takes the start lock, launches its chunk, holds the lock HOLD seconds while the
# simulator connects, and releases it - two starting together lose runs to "Could not start server".
lane() {
    local list=$1 id=$2 c pid
    rm -rf "$W/lanes/ch$id"; mkdir -p "$W/lanes/ch$id"; split -l "$CHUNK" -d -a 3 "$list" "$W/lanes/ch$id/c"
    for c in "$W"/lanes/ch$id/c*; do
        until mkdir "$W/lanes/startlock" 2>/dev/null; do sleep 1; done
        if [ "$BOX" = windows ]; then
            /c/ti/ccsv5/eclipse/eclipsec.exe -nosplash -data "$(wp "$W/ws/lane$id")" -application com.ti.ccstudio.apps.runScript \
                -dss.rhinoArgs "$(wp "$W/runbatch.js") $(wp "$W/$CCXML") $(wp "$c")" >> "$W/lanes/lane$id.log" 2>&1 &
        else
            "$HOME/ti/ccsv5/ccs_base/scripting/bin/dss.sh" "$W/runbatch.js" "$W/$CCXML" "$c" >> "$W/lanes/lane$id.log" 2>&1 &
        fi
        pid=$!; sleep "$HOLD"; rmdir "$W/lanes/startlock"; wait $pid
        echo "$(basename "$c") $?" >> "$W/lanes/lane$id.status"
    done
}
# A run is unmeasured when its result says failed, or when it has no result at all (its lane died).
failed() { local r; for r in $(awk '{print $3}' "$W/lanes/all.txt" | sed 's|^C:|/c|'); do
    if [ ! -f "$r" ] || grep -q "RESULT failed" "$r"; then echo "$r"; fi; done; }
# Join the lanes, but not past DEADLINE seconds: a lane still running then is killed with its JVM.
join() {
    local end=$(( $(date +%s) + $1 ))
    while [ -n "$(jobs -rp)" ]; do
        if [ "$(date +%s)" -ge "$end" ]; then
            log "sim: deadline reached - killing $(jobs -rp | wc -l) lanes still running"
            kill $(jobs -rp) 2>/dev/null
            if [ "$BOX" = windows ]; then taskkill //F //IM eclipsec.exe //T > /dev/null 2>&1
            else pkill -f "ccsv5.*eclipse" 2>/dev/null; fi
            break
        fi
        sleep 5
    done
    wait 2>/dev/null
}

sim() {
    rm -rf "$W/lanes"; mkdir -p "$W/lanes"
    local all=$W/lanes/all.txt
    # plan-sim.txt is the simulator's list already (o2run.py wrote it); only images that were built are run.
    awk '{print $1}' "$W/plan-sim.txt" | sed 's|^C:|/c|; s|/image.out$|/status|' > "$W/lanes/st.txt"
    paste -d' ' "$W/lanes/st.txt" "$W/plan-sim.txt" | while read -r st line; do
        v=; read -r v < "$st" 2>/dev/null; [ "$v" = ok ] && echo "$line"; done > "$all"   # read: no process per line
    # The long runs first, dealt round the lanes so each lane carries at most its share of them.
    { grep -E 'harnessFile|-- prog' "$all"; grep -vE 'harnessFile|-- prog' "$all"; } |
        awk -v L="$LANES" -v D="$W/lanes" '{print > (D "/lane" (NR-1)%L ".txt")}'
    log "sim: $(wc -l < "$all") runs on $LANES lanes, one JVM start at a time (${HOLD}s), $CHUNK runs a JVM"
    local i=0 f
    rmdir "$W/lanes/startlock" 2>/dev/null
    for f in "$W"/lanes/lane*.txt; do lane "$f" $i & i=$((i + 1)); sleep "$STAGGER"; done
    join "$DEADLINE"
    local bad; bad=$(failed | wc -l)
    if [ "$bad" -gt 0 ]; then
        log "sim: $bad runs failed to start - retried on $LANES lanes"
        failed > "$W/lanes/retry.txt"; xargs rm -f < "$W/lanes/retry.txt"
        grep -F -f <(sed 's|^/c|C:|' "$W/lanes/retry.txt") "$all" | awk -v L="$LANES" -v D="$W/lanes" '{print > (D "/retry" (NR-1)%L ".txt")}'
        i=0; for f in "$W"/lanes/retry*.txt; do [ "$f" = "$W/lanes/retry.txt" ] && continue; lane "$f" r$i & i=$((i + 1)); done
        join 600
    fi
    failed > "$W/lanes/unmeasured.txt"
    for f in "$W"/lanes/lane*.status; do awk -v L="$(basename "$f" .status)" '$2 != 0 {print "lane " L " chunk " $1 " exited " $2}' "$f"; done > "$W/lanes/lanes-failed.txt"
    log "sim: $(wc -l < "$W/lanes/unmeasured.txt") runs unmeasured, $(wc -l < "$W/lanes/lanes-failed.txt") lane chunks exited non-zero"
}

export -f build1 vm1 meta wp
export W BOX CPP11 ASM6X LNK6X VM CG744 LIB744 CG822 LIB822
log "o2run $BOX: $(wc -l < plan-build.txt) builds, $(wc -l < plan-sim.txt) simulator runs planned"
PHASES=${PHASES:-info examples build vm sim}
case " $PHASES " in *" info "*) log "phase info"; info;; esac
case " $PHASES " in *" examples "*) log "phase examples"; examples;; esac
case " $PHASES " in *" build "*) log "phase build"; xargs -P "$JOBS" -n 2 bash -c 'build1 "$0" "$1"' < plan-build.txt;; esac
case " $PHASES " in *" vm "*) log "phase vm"; grep ' cpp11-' plan-build.txt | xargs -P "$JOBS" -n 2 bash -c 'vm1 "$0" "$1"';; esac
case " $PHASES " in *" sim "*) log "phase sim"; sim;; esac
log "done"
touch "$W/o2run.done"
