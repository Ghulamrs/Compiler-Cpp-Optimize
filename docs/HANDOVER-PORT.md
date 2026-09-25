# Handover: the port from Compiler-Cppi, 2026-09-25

Branch `ride-4.5` from `e969743`, not pushed. `docs/PORT-FROM-CPPI.md` is the
ledger - every Compiler-Cppi commit since `6d7386a` with its verdict (71
taken, 23 already present, 16 skipped as its optimizer, 7 skipped for another
stated reason, 11 merges) - and the two constraints the port was held to.
This file says what was measured at the head, and what a later session picks
up.

## Done

All four milestones of the brief: the rename to cpp11 (`src/Name.h`, the
`CPP11_*` variables, the project files RIDE's generator writes); the tms6747
target with every Compiler-Cppi correction, the driver's asm6x/lnk6x path on
this driver's pool, `tests/tms6747.sh`, `tests/asm6x.sh`,
`tools/windows/ti-link.cmd`; the A-series and language features through
virtual inheritance and the cl review's last rows; the `.nocl` for
`inline-overlaid-frames` and the RTTI records held out of names-vs-cl as a
recorded difference. Four faults were found and mended on the way (the
member-pointer alignment on the C6000, `ALIGN 64` under MASM's `.DATA`, the
16-byte function alignment Compiler-Cppi had added, and ELF weak functions
never folding - the last two are the speed floor's business and are in the
ledger's "The floor, measured").

## What a later session should know

- **RIDE's `tools/make-projects.py` must add `src/optimizer`** to cpp11's
  source glob (`src`, `src/parser`, `src/backend`); the project files here
  already list it, so its `--check` will call them stale until it does.
- **`x86_64-windows` through `-masm=gnu` is the default and was not run
  through `tools/verify-three`** this session; the box legs were run by the
  scripts under `C:\cxxopt\port\` (copies of `C:\cxxopt\`'s pointed at the
  port tree), which is where their logs are.
- **Compiler-Cppi's `5592bed`** (no `mov $0, %rax` before a non-variadic
  call at -O0) is the one x86_64 codegen change left out on purpose; the
  optimizer answers it at -O1/-O2, and a -O0 change is a decision with
  numbers.
- The C6000 target runs unoptimized at every level, as in Compiler-Cppi; the
  optimizer has no hook for it and needs none.
- `CXX1_DUMP_MIR` is `CPP11_DUMP_MIR` now; the docs that named it were
  updated.

## Measured at the head

Filled in below from the runs of 2026-09-25 evening; every number is the
run's own last line.

**Mac** (`~/cxxopt-build/port2`, clang, the head less the comment commit):
run.sh 532/0, emit.sh 1305/0 over four targets, names.sh 336/0, overload.sh
30 agreed, comment-lines 63 groups (the cap); tests/tms6747.sh on the
emulator built from VM6747/Emulator 325 passed / 0 failed / 7 skipped for a
64-bit long / 4 not for this target; tests/asm6x.sh 332 objects as by hand,
0 failed. Compiler-Cppi's own were 319 and 309.

**EC2 Linux** (g++ under `./build -j2`, the tree at `f460e2d`, `/tmp/gate.log`
on the box, made by `~/cxxopt-build/linux-gate.sh`): run.sh 532/0, emit.sh
1305/0, names.sh and overload.sh skip themselves there; every case at -O1
and -O2: 331 passed and 4 "differ" at each level, the four being the
abort-by-design cases (catch-copy-throws, dtor-throws-unwinding,
noexcept-terminates, -callee) whose captured stream carries `timeout`'s
"dumped core" line - run.sh closes its stderr for exactly that and reads
them as passes; asm6x.sh 332/0; tms6747.sh 325/0 on RIDE's vm6747;
Compiler++ built at both levels, its four suites 129/67/90/9 at each, 258 of
258 cases identical to g++ -O2's build, the linked `.text` and the bench
in the ledger's floor table. The core dump the aborts left was deleted, and
the scratch tree's obj/ and tests/out-* with it.

**Windows box** (`C:\cxxopt\port\tree` at `f460e2d`, cl `/W4 /WX`):
cases 303/0 at each of -O0, -O1, -O2 through the GNU spelling and 303/0 at
each through the MASM spelling and the project's assembler
(`cases.txt`, `cases-masm.txt`); ti-link.cmd and Compiler++ against cl:
ti-link.cmd 331 programs linked by lnk6x against rts6740_elf_eh.lib under
CCS 7.4, 0 failed, 5 not for this target; Compiler++ built by the port at
-O1 and -O2 (`C:\cxxopt\port\opt-port2.txt`): the four suites 129/67/90/9 at
each level, 258 of 258 cases identical to cl /O2's build, `.text` 518,766 and
638,590 (base 558,030 and 688,814), built in 1,861 and 2,433 ms (base 1,688
and 2,327), bench.cpp best of five 992 and 593 ms (base 1,047 and 597).

**Not run this session**: `tools/verify-three` itself (its Windows leg
unpacks into `C:\cxx1\verify`, which the brief kept me out of); the
names-vs-cl comparison after the RTTI filter, for the same reason.
