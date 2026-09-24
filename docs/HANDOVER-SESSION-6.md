# Handover to session 7: the stack machine's temporaries are the allocator's

Fable 5.1, 2026-09-24, end of session 6 on branch
`claude/fable-5-1-background-work-kguwml`, from `e8a975b` (session 5's
head). A Linux container: no Mac, no Windows box, no cl, no Compiler++
tree. Everything below was measured here; the last section says what the
box has to run.

Read first: `docs/OPTIMIZER-ARCHITECTURE.md` (kept in step: the walker's
temporaries under `Optimizer`, `pairTemp`/`pairXmm` under forward-values,
`shrinkTemporaries` under finish-frame), then `docs/HANDOVER-SESSION-5.md`
for S6 and the numbers this session started from.

## What landed, in order

| Commit | Step | What it is |
|---|---|---|
| `4cdb3e9` | S7a | Integer temporaries in frame slots at -O1/-O2: `push()`/`pop()` write and read a slot of a per-function temp stack; `Optimizer::temporaries` places the region below every frame and lists the slots as locals; forward-values pairs a slot's load with its store (`pairTemp`); finish-frame shrinks the region to what survived |
| `9d937e2` | S7b | Floating temporaries the same way: `pushF()`/`popF()` are one `movsd` each; `pairXmm` resolves a pair through the register that still holds it, or an xmm the function never names |

### The decision, and why it went this way

Session 5 left S7 at a design point: a push/pop pair is what the walker's
call-alignment pads and the atFloor choice were computed against, so a
pass that removed pairs would have to re-derive both for every call the
pair spanned, and the Windows unwind data beside them. Read closely, the
unwind data was never at stake - neither target emits CFI or unwind codes
for a push in the body, the CFA being rbp-based - but the pads are, and
`depth_` decides them. So **the walker does not push.** Where the
optimizer holds the function and no funclet reads the frame, a temporary
goes to a frame slot, `depth_` counts the real stack alone, and the pads,
the outgoing area, the funclet's 32 bytes, the landing pads and setjmp are
exactly what they were at -O0. A stack argument keeps a real push
(`pushArg`, `pushFArg`); the x87 pair stays on the stack; a function with
Microsoft funclets keeps push and pop throughout, its frame being unable
to grow once a funclet is out. -O0 is byte-identical: `identical.sh` at
`LEVELS=0` compares 574 outputs and none differ, `emit.sh` reads 0 of 839
against the golden recorded at `5e6e2e3` (which e8a975b's build matches).

### What it took beyond the obvious

- **Where the region goes.** The first placement put the temporaries
  beside the inlined callees' frames, and `return-owned-temporary` at -O2
  printed 98 for 2: a temporary's slot lay inside an inlined `Buf` whose
  address was taken, and `promotableLocals` marks only the `lea`'d word as
  escaped, so the object's second word was read through the temporary's
  pseudo. The slots are addressed at `-(2^40 + 8(t+1))` while the walk
  runs and placed below `frameBase()` once it is done - below every local
  and every inlined frame - so a temporary never shares an address with an
  object.
- **forward-values had to see the pair.** It already turned an in-block
  push/pop into a copy (`pairPop`); a slot round trip was opaque to it, and
  the size proxy rose 7-14% while loop counters lost their registers - the
  `lea` of `i` for `i++`, which the pair had carried to `fold-offsets`,
  survived to `locals` in the slot. `pairTemp` is `pairPop` for a slot
  (`pairWith` the shared body), and it runs *before* `reloadFromRegister`,
  which otherwise resolves the load and leaves the store standing.
- **The frame shrinks back.** Reserving the region by the walk's high-water
  mark left `sub $32` where `sub $16` had been, and on Windows every
  operand is rendered against the frame size. `shrinkTemporaries` in
  finish-frame sizes the region by the slots still named and
  `dropUnusedSaves` re-places the saves below it; for a function without
  temporaries the arithmetic is the old one.
- **An xmm slot is paired, not promoted.** `promotableLocals` refuses an
  xmm access and this session does not change that. `pairXmm` drops the
  pair where the register still holds the value untouched, copies with
  `movapd` where another does, and otherwise carries it through an xmm
  register the function never names (`xmmScratch_`, all sixteen on SysV,
  xmm0-5 on Microsoft). An instruction the effects table has no row for is
  opaque - every register touched - so the untouched test is conservative.
  A pair across a call stays two `movsd` to a slot: no rsp traffic.

## Measured here

| Check | S7a (`4cdb3e9`) | S7b (`9d937e2`) |
|---|---|---|
| every case at -O1 and -O2, host, run and diffed | 574 / 0 | 574 / 0 |
| `bash tests/run.sh` | 476 / 0 | 476 / 0 |
| `tests/emit.sh` (the -O0 golden) | 0 of 839 changed | 0 of 839 changed |
| `tests/overload.sh`, `tests/names.sh` | 30 / 0, 287 / 0 | 30 / 0, 287 / 0 |
| `tools/identical.sh`, `LEVELS=0`, against e8a975b | 574 / 0 differ | 574 / 0 differ |
| `tools/comment-lines --count` | 63 | 63 |
| `-g -O2` on three cases; `-masm=masm -O2` emits | yes | yes |

The size proxy (every case emitted at a level for x86_64-linux and
x86_64-windows, assembled by clang for its target, `.text` summed):

| | e8a975b | S7a | S7b |
|---|---|---|---|
| x86_64-linux -O1 | 319,746 | 317,776 | **313,464** (-2.0%) |
| x86_64-linux -O2 | 386,851 | 383,473 | **378,739** (-2.1%) |
| x86_64-windows -O1 | 319,773 | 313,969 | **310,430** (-2.9%) |
| x86_64-windows -O2 | 379,487 | 374,210 | **370,145** (-2.5%) |

Instruction lines over the same emissions, x86_64-linux -O2: 109,241 to
104,278 (-4.5%). What the two steps took out of them: `push %rax` lines
1,969 to 457, `sub $8, %rsp` pads 1,379 to 58, `movsd %xmm0, (%rsp)` 310
to 5; 188 `movapd` came in. The saves rose from 5,111 to 5,636 - a
temporary across a call that earns a preserved register by the level's
weights takes one, as a local does.

The kernels' bench (`tools/windows/bench-kernels.cpp`, `-O2`, nine
interleaved rounds, medians in ms, checksums equal throughout). Three
kernels - sieve, isort, hash - emit byte-identically under both
compilers, so any movement in those rows is placement and noise, and this
container is noisy (the same binary's runs spread 20% on a bad round).
With every label of both assemblies aligned to 16 (`sed` on the labels,
`c++ -no-pie`), which is the only fair comparison here:

| | g++ -O2 | e8a975b, plain | S7b, plain | e8a975b, aligned | S7b, aligned |
|---|---|---|---|---|---|
| fib | 3 | 14 | 14 | 14 | 13 |
| sieve | 60 | 92 | 100 | 88 | 89 |
| matmul | 10 | 57 | 53 | 60 | **53** |
| isort | 9 | 37 | 38 | 36 | 37 |
| hash | 216 | 341 | 340 | 348 | 357 |
| virtual | 154 | 218 | 211 | 223 | 206 |
| total | 468 | 762 | 747 | 777 | 755 |

Read this way: **matmul is the one kernel whose loop changed** - six
stack round trips a turn became register copies - and it moves 60 to 53
aligned. **fib is a wash**, as its code says: the `push; sub; add; pop`
around the recursive call became `mov %eax, %r12d` and `add %r12d, %eax`,
plus a save and a restore of r12 per call, four instructions either way;
g++'s 3 comes from transforming the recursion, not from the temporary.
**virtual** is the placement effect session 5 already recorded (the base
moved 218 to 247 by alignment alone in one run). The unaligned S7b sieve
at 100 is a binary that drew the noisy slots: its fast half matches the
base to the millisecond, and the code is identical.

## What the box must run to judge it

From `C:\cxxopt`, as sessions 3 to 5 laid it out:

1. `sync.cmd` with a bundle of this branch's head (`9d937e2`), then
   `msvc\build.cmd`.
2. `opt.cmd s7 O1 O2`: Compiler++ at both levels, its four suites, 258
   cases against `C:\cxx1dev\w\cl-O2`. The gate: 258/258 and green.
3. `cases.cmd O0 O1 O2`: cxx1's cases on the box. **This is the first time
   the temporaries run under a Windows CRT** - the temp slots are rendered
   against the grown frame like the saves, the variadic double's register
   copy reads the slot, and a function with funclets keeps push and pop;
   none of that has executed here. The `try`, RTTI and by-value shapes are
   what to watch.
4. `.text` of Compiler++ at -O1 and -O2, GNU and MASM routes. The proxy
   says both fall 2-3% against session 5's.
5. `ab.cmd 15 base-O2 s7-O2` and `ab.cmd 15 base-O1 s7-O1`. The rule
   stands: a behaviour change measured at nothing is reverted, but a size
   fall with the bench unchanged is a result.
6. `tools\windows\bench.cmd` for the kernels against cl /O2 - matmul is
   the row to expect movement in.
7. On the Mac, `tools/identical.sh` with the Compiler++ tree for the "no
   output with more instructions" check over its units, and the
   `-masm=masm` route assembled by ml64, which only emits here.

## Next, in order

- **A temporary across a call at -O1.** It takes a preserved register when
  its two accesses reach `minWeight` in a loop, and the save and restore
  cost more bytes than the two `mov`s to the slot; `SizeCosts` should
  charge the save in bytes, which is Z1's cost again.
- **xmm slots as pseudos**: `promotableLocals` refuses an xmm access, so a
  double held across a call is a slot round trip. An SSE palette in the
  allocator is what lifts it, and matmul's `x` at `-32(%rbp)` is the case.
- The loop counters matmul keeps in memory (`-20(%rbp)`, `-36(%rbp)`) are
  not this session's: a `cmpl $n, slot` folded by fold-loads is not a
  renamable access, and the reference has them the same way.
- The inliner's callees with a `switch` or a goto label (session 3's item),
  and Z1's bytes cost.
