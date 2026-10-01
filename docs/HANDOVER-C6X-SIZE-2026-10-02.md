# Handover: the C6000 size level, 2026-10-02

Branch `c6x-matmul-sieve`, worktree `.claude/worktrees/agent-aabb1b1146ee29986`, stopped at the
user's session limit. Nothing pushed, nothing merged. Head is `b2062c1` (WIP) on top of `956b090`.

## Done, measured on the Mac only

**Review of the WIP commits 03325fd / 469d0e2 / 6135bcd (frame-slot reuse).** Read in full. Every
`frameSize_ = 0` site that synthesises a function mid-parse goes through `openFrame`/`closeFrame`
(14 in ParserClass/ParserInit/ParserVtable); `topLevel` clears `freeSlots_`; `captureFunctionState` /
`restoreFunctionState` / `clearFunctionState` carry `freeSlots_` and `localSerial_`, so the lambda
return deduction, replayed bodies and `enterInitFunction` are covered. Two things noted, neither
changed: `resolveGotos` still matches a goto's alive objects to a label's by **offset**, which two
objects sharing a slot could confuse - safe today because `checkJump` (by serial) refuses any such
jump first, since an object in `alive_` always has a destructor and so `guardsJump`; and the init
function's freed slots are dropped on `leaveInitFunction` (a size cost of nothing, never a reuse
across frames). The WIP messages were **not** rewritten yet - do that (`git rebase -i 179c590~1`
is not possible without touching the merge; reword 03325fd/469d0e2/6135bcd/b2062c1 only).

**The size level, `-Os`** (commit b2062c1):
- Driver: `-Os` is `optimize_ = 1` plus `forSize_`; `CodeGen::setOptimizeForSize(bool)` (Backend.h),
  default no-op, so every other target takes -Os as -O1. Usage text says so.
- tms6747 (`src/backend/Tms6747.cpp`): `constDivisor` keeps `__c6xabi_divi/remi/divu/remu` for a
  divisor that is not a power of two **when the first walk found a call** (`planHasCall_`, set after
  the planning walk; both walks then agree) - in a leaf the helper would cost B3, the frame and the
  callee-saved set and measured no smaller (hash 384 either way); `earlyExit` (the duplicated entry
  test) is off under `forSize_`.
- **A global's address in a register at -O1/-Os** (`globalUses_`, `globalReg_`, `planRegisters`,
  `genAddr`, materialised in the prologue after the saves; `earlyExit` hides the map since it runs
  ahead of the prologue). Weighted at two words a use, candidates need `uses >= 4` (two formings).
  **-O2 excluded on purpose**: with it, matmul's inner loop lost its pipeline schedule (158 k ->
  352 k emulator cycles) and isort's `no schedule under four fifths`; -O2 output is byte-identical
  to HEAD for all six kernels.
- **`foldScaledIndex`** in `src/optimizer/C6xSched.cpp` (every level, `CPP11_C6XSKIP=index` to
  skip): `SHL I, log2(w), T; ADD R, T, U; LD/ST *U` -> `*+R[I]`, several readers of one SHL, the
  `MV I, T` form when I is stepped first. `-O2` kernel output unchanged (no pattern survives the
  pipeliner there), so -O2 cycles are untouched by construction.

**Measured (vm6747 -c, scratch build, all six outputs equal their .expected at -O1/-Os/-O2):**

| kernel | O1 before | O1 now | Os | O2 (unchanged) | bytes O1 before -> O1 / Os | 744 -ms3 | Os ratio |
| --- | --- | --- | --- | --- | --- | --- | --- |
| fib | 1,748,060 | 1,748,060 | 1,748,068 | 1,748,060 | 192 -> 192 / 160 | 128 | 1.25 |
| hash | 5,228,050 | 5,228,050 | 5,228,050 | 2,276,050 | 384 -> 384 / 384 | 256 | 1.50 |
| isort | 2,466,252 | 1,958,730 | 1,957,530 | 1,851,296 | 640 -> 544 / 512 | 352 | 1.45 |
| matmul | 501,187 | 459,740 | 459,740 | 157,819 | 704 -> 672 / 672 | 416 | 1.62 |
| sieve | 2,843,539 | 2,390,619 | 2,390,619 | 1,415,557 | 512 -> 480 / 480 | 352 | 1.36 |
| virt | 1,000,294 | 1,000,294 | 1,000,294 | 1,000,243 | 1,184 -> 1,184 / 1,184 | 640 | 1.85 |

cl6x objects and assembly for all six (7.4.4 `-O2`, `-O2 -ms3`, 8.2.2 `-O2 -ms3`) are on the box in
`C:\cxx1\fable-c6x-size\` (go2.cmd there regenerates them); 744-O2 bytes 128/352/480/1152/416/672.

## Not run yet (the gates) - run before anything else

run.sh at -O0/-O1/-O2 (and -Os), `tests/emit.sh` golden (record at 956b090 first; expect the -O1
tms6747 emissions to move, -O2 and the hosts not at all), names.sh, overload.sh,
`CXX1_FLAGS=-O0|-O1|-O2|-Os tests/tms6747.sh`, `tools/c6747-levels` on the box (it builds only O1/O2:
add `Os` to its level loop and to bench-levels.cmd's `for %%l in (O1 O2)` to get an -Os column),
the Compiler++ harness at -O1 (the global-register change touches it). `make comments` is 0.

## Why the kernels are still over 1.3x, by kernel (read before deciding more)

- **fib 1.25x**: fine. cl6x's 128 relies on `__c6xabi_call_stub` (caller-saved registers preserved
  by a stub), not reproducible without that scheme.
- **hash 1.5x**: cl6x keeps the return address in B9 and lets `__c6xabi_remi` preserve everything
  else, so its leaf needs no frame; cpp11's magic divide is 10 words. The remaining idea: at -Os in a
  non-caller, call the helper and keep B3 in a caller-saved register the helper preserves (needs the
  helper's clobber list from the EABI: A0-A2, A4, A6?, B0-B2, B4, B5, B30, B31 - verify in rtssrc).
- **isort 1.45x, matmul 1.62x**: `rnd()` not inlined at -O1 (cl6x -ms3 inlines a static called
  once - a `called-once static` rule in the Inliner at -Os would delete the body: ~-8 words in isort);
  `MVK 0; INTDP; NOP` for `0.0` should be `ZERO A5:A4`; row bases `MPY32 i,192` recomputed per
  access in matmul (-O1 has no hoisting); the second access in matmul's inner loop keeps its ADD
  because its U is reused by the store through `MV A23, A6`.
- **sieve 1.36x**: the 64-bit `j` loop (`long long`) is 20 words of pair arithmetic; cl6x narrows it.
- **virt 1.85x**: the unwind pads and the `$s0` stubs (~30 words) are code cl6x has none of without
  `--exceptions`; measure cl6x `-O2 -ms3 --exceptions` for the honest ratio (not done). Parameters
  used once are stored to the frame and reloaded (`STW B4; LDW; NOP 4; STW`): the planner needs
  `uses >= 2`; a parameter with one use after a call should take a callee-saved register.

## Exact next steps

1. Run the gates above; fix anything red; re-record the golden; measure c6747-levels on the box
   (add the Os column); re-measure the Compiler++ harness at -O1 (`tools/c6747/compilerpp`).
2. Reword the four WIP commit messages; write the CLAUDE.md section (table above, dated 2026-10-02,
   the -O2 exclusion of the global registers and why, the fold, the per-kernel reasons).
3. If size is still wanted: inline a called-once static at -Os; `ZERO` for a 0.0 constant; then the
   B3-in-B9 scheme for leaf helpers.
