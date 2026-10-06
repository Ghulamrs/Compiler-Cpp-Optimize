#!/bin/sh
# The tms6747 backend, run on the VM6747 emulator against the corpus's own
# expected output: every case in tests/cases that is meant to compile is
# compiled with cpp11 for the C6000, run on vm6747, and its stdout and stderr
# must match tests/cases/<case>.expected, as tests/run.sh requires of the
# host target. The cases that are meant to fail to compile (<case>.error)
# are the front end's and are not run here.
#
# Two lists of cases are skipped by name, each with its reason beside it:
# tests/tms6747-exceptions.txt, the cases that throw or need a landing pad,
# which this target does not unwind yet; and tests/tms6747-lp64.txt, the
# cases whose expected output was written by a 64-bit-long host. A case whose
# .notarget names tms6747 is skipped too, with its reason, as run.sh does for
# the host: it is a construct refused by name for this target.
#
# Each case runs twice: its assembly on vm6747, and as a TI program - asm6x, then lnk6x against
# TI's runtime - on vm6747sim, which runs the machine code. Both must match. The second leg is
# what sees an assembler's encoding and TI's own runtime (C1, C2, D2 hid from the emulator).
# SIM=0 leaves it out, said aloud; tests/tms6747-sim.txt names the cases it cannot run, why beside.
set -u

cd "$(dirname "$0")/.."
CXX1="${CXX1:-./cpp11.exe}"
# The emulator where the VM6747 checkout keeps it, or beside this tree as it was.
if [ -z "${VM:-}" ]; then
    for v in ../VM6747/Emulator/vm6747.exe ../Emulator/vm6747.exe; do [ -x "$v" ] && { VM=$v; break; }; done
    VM=${VM:-../VM6747/Emulator/vm6747.exe}
fi
# CYCLES=1: each case's cycles from main, as TI's simulator counts cycle.CPU (vm6747 -c), taken
# out of its output before the comparison and kept in <case>.cycles; the total is printed.
CYCLES="${CYCLES:-}"
# The second leg's tools: each named, else where the sibling checkouts keep it, else on PATH.
SIM="${SIM:-1}"
find_tool() {
    for t in "$@"; do [ -n "$t" ] && [ -x "$t" ] && { echo "$t"; return; }; done
}
ASM6X=$(find_tool "${ASM6X:-}" ../ASM6x/build/asm6x.exe "$(command -v asm6x 2>/dev/null)")
LNK6X=$(find_tool "${LNK6X:-}" ../LNK6x/build/lnk6x.exe "$(command -v lnk6x 2>/dev/null)")
VMSIM=$(find_tool "${VMSIM:-}" ../VM6747-sim/vm6747.exe "$(command -v vm6747sim 2>/dev/null)")
# The run-time library: RTS6x's, built beside this tree, since it passed the suite whole (M5, 2026-10-07);
# TI's exception-handling rts6740 as RTSLIB=rts6740_elf_eh.lib, TIRTS its directory (default ~/c6747-lib).
RTSLIB="${RTSLIB:-rts6x.lib}"
if [ "$RTSLIB" = rts6x.lib ]; then TIRTS="${TIRTS:-../RTS6x/build}"; else TIRTS="${TIRTS:-${C6747_EHLIB:-$HOME/c6747-lib}}"; fi
CXX1_FLAGS="${CXX1_FLAGS:-}"   # -O1 or -O2 runs the corpus through the C6000 optimizer
OUT=tests/out-tms6747
# One case by name, or all of them; a worker is handed the name it was asked for after --one.
if [ "${1:-}" = --one ]; then only=$2; else only="${1:-}"; fi
# One case - compiled for the C6000 and run on vm6747 - its report and verdict written beside its
# output, so that the cases run at once: JOBS of them, the machine's processors by default.
one() {
    base=$1; src=tests/cases/$base.cpp
    # `<case>.part.cpp` is the second translation unit of <case>, built into its program, not a case.
    case "$base" in *.part) return ;; esac
    if [ -f "tests/cases/$base.notarget" ] && grep -q "^tms6747[[:space:]]" "tests/cases/$base.notarget"; then
        echo "  skip $base for tms6747: $(grep "^tms6747[[:space:]]" "tests/cases/$base.notarget" | sed 's/^tms6747[[:space:]]*//')"
        echo notarget > "$OUT/$base.verdict"; return
    fi
    if [ -z "$only" ] && grep -q "^$base[[:space:]]" tests/tms6747-exceptions.txt; then echo eh > "$OUT/$base.verdict"; return; fi
    if [ -z "$only" ] && grep -q "^$base[[:space:]]" tests/tms6747-lp64.txt; then echo lp64 > "$OUT/$base.verdict"; return; fi

    if ! ( ulimit -t 10; "$CXX1" -S -arch tms6747 -nologo $CXX1_FLAGS "$src" -o "$OUT/$base.s" < /dev/null ) 2>"$OUT/$base.err"; then
        echo "FAIL $base: cpp11 refused it"
        sed 's/^/      /' "$OUT/$base.err" | head -3
        echo fail > "$OUT/$base.verdict"
        return
    fi
    parts=""
    if [ -f "tests/cases/$base.part.cpp" ]; then
        if ! ( ulimit -t 10; "$CXX1" -S -arch tms6747 -nologo $CXX1_FLAGS "tests/cases/$base.part.cpp" -o "$OUT/$base.part.s" < /dev/null ) 2>>"$OUT/$base.err"; then
            echo "FAIL $base: cpp11 refused its .part.cpp"; sed 's/^/      /' "$OUT/$base.err" | head -3
            echo fail > "$OUT/$base.verdict"; return
        fi
        parts="$OUT/$base.part.s"
    fi
    if [ -n "$CYCLES" ]; then
        { "$VM" -c "$OUT/$base.s" $parts > "$OUT/$base.raw" 2>&1 < /dev/null; } 2>/dev/null || true
        # The program's last line may have no newline, so the count is cut out exactly as written.
        grep -o 'CYCLES count=[0-9]* packets=[0-9]* natives=[0-9]*' "$OUT/$base.raw" | sed 's/^CYCLES //' > "$OUT/$base.cycles"
        perl -0pe 's/CYCLES count=\d+ packets=\d+ natives=\d+\n//' "$OUT/$base.raw" > "$OUT/$base.out"
    else
        { "$VM" "$OUT/$base.s" $parts > "$OUT/$base.out" 2>&1 < /dev/null; } 2>/dev/null || true
    fi
    verdict=pass
    if ! diff -q "tests/cases/$base.expected" "$OUT/$base.out" >/dev/null; then
        echo "FAIL $base:"
        diff "tests/cases/$base.expected" "$OUT/$base.out" | sed 's/^/      /' | head -8
        verdict=fail
    fi
    if [ "$SIM" = 1 ]; then
        if [ "$RTSLIB" != rts6x.lib ] && grep -q "^$base[[:space:]]" tests/tms6747-sim.txt; then
            [ "$verdict" = pass ] && verdict=simskip
        elif ! { "$ASM6X" "$OUT/$base.s" -o "$OUT/$base.obj" &&
                 { [ -z "$parts" ] || "$ASM6X" "$parts" -o "$OUT/$base.part.obj"; } &&
                 "$LNK6X" -mv6740 --abi=eabi -i "$TIRTS" "$OUT/link.cmd" "$OUT/$base.obj" ${parts:+"$OUT/$base.part.obj"} \
                     -l "$RTSLIB" -o "$OUT/$base.ti.out"; } > "$OUT/$base.ti.log" 2>&1 < /dev/null; then
            echo "FAIL $base (vm6747sim): asm6x or lnk6x refused it"
            sed 's/^/      /' "$OUT/$base.ti.log" | head -3
            verdict=fail
        else
            { ( ulimit -t 20 2>/dev/null; "$VMSIM" --run "$OUT/$base.ti.out" ) > "$OUT/$base.sim.out" 2>&1 < /dev/null; } 2>/dev/null || true
            if ! diff -q "tests/cases/$base.expected" "$OUT/$base.sim.out" >/dev/null; then
                echo "FAIL $base (vm6747sim):"
                diff "tests/cases/$base.expected" "$OUT/$base.sim.out" | sed 's/^/      /' | head -8
                verdict=fail
            fi
        fi
    fi
    echo $verdict > "$OUT/$base.verdict"
}
if [ "${1:-}" = --one ]; then one "$3" > "$OUT/$3.report" 2>&1; exit 0; fi
rm -rf "$OUT"; mkdir -p "$OUT"
if [ ! -x "$VM" ]; then echo "tms6747.sh: no emulator at $VM"; exit 1; fi
if [ "$SIM" = 1 ]; then
    for need in "asm6x:$ASM6X" "lnk6x:$LNK6X" "vm6747sim:$VMSIM"; do
        [ -n "${need#*:}" ] || { echo "tms6747.sh: no ${need%%:*} - build it, name it, or SIM=0 to leave the vm6747sim leg out"; exit 1; }
    done
    [ -f "$TIRTS/$RTSLIB" ] || { echo "tms6747.sh: no $RTSLIB in $TIRTS - set TIRTS and RTSLIB, or SIM=0"; exit 1; }
    # RIDE's flat map, with a stack a case may need (stack-probe takes 256 KB).
    cat > "$OUT/link.cmd" <<'MAP'
--rom_model
--stack_size=0x100000
--heap_size=0x100000
MEMORY { RAM : origin = 0xC0000000, length = 0x04000000 }
SECTIONS
{
    .text > RAM  .const > RAM  .data > RAM  .bss > RAM  .far > RAM  .fardata > RAM
    .neardata > RAM  .rodata > RAM  .cinit > RAM  .init_array > RAM  .switch > RAM
    .cio > RAM  .stack > RAM  .sysmem > RAM  .vm6747.eh > RAM
}
MAP
else
    echo "tms6747.sh: SIM=0 - the vm6747sim leg is left out; only the assembly is run, on vm6747"
fi
cases() {
    for src in tests/cases/*.cpp; do
        base=$(basename "$src" .cpp)
        [ -n "$only" ] && [ "$base" != "$only" ] && continue
        [ -f "tests/cases/$base.error" ] && continue
        echo "$base"
    done
}
JOBS=${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)}
cases | xargs -P "$JOBS" -I{} sh "$0" --one "$only" {}
for base in $(cases); do cat "$OUT/$base.report"; done
count() { cat "$OUT"/*.verdict 2>/dev/null | grep -cx "$1" || true; }
pass=$(( $(count pass) + $(count simskip) )); simskip=$(count simskip); fail=$(count fail); skipEh=$(count eh); skipLp=$(count lp64); skipNt=$(count notarget)

echo "tms6747.sh: $pass passed, $fail failed, $skipEh skipped for exceptions, $skipLp skipped for a 64-bit long, $skipNt not for this target"
[ "$SIM" = 1 ] && echo "tms6747.sh: every pass also on vm6747sim (asm6x, lnk6x, $RTSLIB) but $simskip in tests/tms6747-sim.txt"
if [ -n "$CYCLES" ]; then
    for base in $(cases); do [ -s "$OUT/$base.cycles" ] && echo "$base $(cat "$OUT/$base.cycles")"; done > "$OUT/cycles.txt"
    awk '{ sub("count=", "", $2); s += $2 } END { printf "tms6747.sh: %d cycles (cycle.CPU) over %d cases, in %s\n", s, NR, FILENAME }' "$OUT/cycles.txt"
fi
[ "$fail" -eq 0 ]
