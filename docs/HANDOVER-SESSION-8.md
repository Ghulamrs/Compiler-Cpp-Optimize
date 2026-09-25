# Handover to session 9: S9, calls and the hash loop

Fable 5.1, 2026-09-24/25, end of session 8 on branch
`claude/fable-5-1-background-work-kguwml`, from `e91a4f6` (session 7's
head). A Linux container: no Mac, no Windows box, no cl, no cl6x, no
Compiler++ tree. Everything below was measured here; the last section
says what the box has to run.

Read first: `docs/OPTIMIZER-ARCHITECTURE.md` (kept in step: the inliner's
self-sites and inlined scalars, `hoist-invariants`, forward-values' three
narrow rewrites, the pipeline declaration), then
`docs/HANDOVER-SESSION-7.md` for S8 and the numbers this session started
from.

## The gate, and how it was applied

S8's rule held: a step lands only if the benchmark total and the kernels it
targets are faster in a placement-controlled comparison and nothing is
slower, size not growing beyond the trivial, every check green. Each
candidate was priced **by editing the emitted assembly by hand and timing
it before anything was written** - and this session priced one candidate
after building it, and did not land it (below).

Two things about the instrument, learned again. `virtual`'s loop body was
byte-identical through every step and it read 146 to 218 ms depending on
what came before it in the image; its column is a lottery and the totals
carry it. And an effect under ~5% of a kernel is invisible one at a time
here (±5 ms on hash's 290): the three hash rewrites each measured within
noise alone and -35 ms together, which is why they landed as one step
priced as a set.

## What landed, in order

| Commit | Step | What it is |
|---|---|---|
| `11d3e3d` | S9a | A function's call of itself is an inlining site (`inPlace_` already stops the nesting, so exactly one level); and an inlined callee's scalars are listed for promotion, shifted by the base, so an inlined parameter or local is a register rather than a slot in the shared region |
| `acedd3c` | S9b | `hoist-invariants`, after `fold-index`: loop-invariant code motion after allocation into registers the loop leaves free, with value numbering, a carried-register budget, and the flow rebuilt per loop so outer loops hoist what inner ones left |
| `906a19c` | S9c | forward-values: `mov $c,%d; add %s,%d` is `lea c(%s),%d` where the flags are dead (the base chased to the 32-bit source of a sign extension); a `movslq` whose extension another register holds is a copy; a byte or word extension read only at its source's width is a copy, which then dies |

### Priced and not landed

- **A three-operand `imul $c, %s, %d`.** Built end to end (an `imm` field on
  `Instr`, an `imul3` opcode of Move kind that sets the flags, `ins3` on
  the spellings - `imul $c, a, b` in GNU, `imul b, a, c` in MASM -
  produced by forward-values from `mov $c,%d; imul %s,%d` and from
  `mov %s,%d; imul $c,%d`, scales 1/2/4/8 left to `shrink` and
  `fold-index`). Both spellings emitted, clang assembled the COFF, the
  hash loop lost two instructions - and measured 289 -> 289, and 261
  against 254 when added to the three rewrites. On this Xeon the fill
  loop is not bound by the uops this saves. Reverted; the shape above is
  what to rebuild if a machine shows it paying.
- **A frameless prologue for fib** (push/pop of rbx and r12, no rbp):
  13 -> 12 ms by hand. One millisecond against touching the CFI, the
  Windows unwind codes and the funclets' frame base: not taken.
- **A second level of recursive inlining** (g++ takes eight): the walker
  cannot nest a body walked in place (`inPlace_` is a bool and
  `inlineBase_` a single base). fib1, the one-level shape written in the
  source, runs at 9 ms; two levels would be its own step in the walker.
- **Loop rotation** was re-checked on hash's fill loop: nothing, as S8
  found on sieve.

## What each cost beyond the obvious

- **The inlined region was deliberately unpromoted** ("every site shares
  that region, so no slot in it is one variable's"). Listing the callee's
  scalars shifted by the base, once per (disp, size), is enough:
  `promotableLocals` already refuses two overlapping accesses that
  disagree, and the same callee at two sites is one slot with two
  disjoint lives, coloured as one pseudo. linux -O2 `.text` fell 3.1%.
- **`hoist-invariants` before `fold-index` refused matmul's chains**: it
  met `add %r9, %rdi; movsd %xmm0, (%rdi)`, a variant read-modify-write
  of the hoisted address. After `fold-index` the operand is indexed and
  the chain goes whole.
- **Two faults in the pass read off the emission**: a destination renamed
  with its base (`movslq (%rax,%rcx,4), %rax` became `(%r11,..), %r11`),
  and a plan whose last step was dropped for want of a register, leaving
  its instruction in the loop reading a value whose defining step had
  been deleted (matmul's init loop segfaulted). Dropping is by whole
  block now - no chain crosses a block - and rarely reached, because the
  budget of carried registers is checked as the plan is made and the
  block is re-planned without the offending definer.
- **What the pass leaves alone, and why**: a `mov $imm` (hoisted, it
  turned hash's `add $1, %r8` back into `add %r9, %r8`, forward-values
  no longer seeing the constant), a zeroing (free), and a frame address
  (forward-values folds it into the operand). Constants and frame
  addresses are what the first version hoisted first.
- **`crowded` in the case**: six invariant rows and two free registers.
  Without the budget the whole block's plan was dropped and nothing
  hoisted; with it, two chains go and four stay.
- **A four-byte copy of an invariant** (`movl %r8d, %r10d` in front of
  `div %r10d`) is its source for readers of four bytes or fewer -
  `divides` in the case is that shape, and without it the loop had one
  free register for two values and hoisted nothing.
- **forward-values never solved liveness** before S9c; it did not need
  to. Reading `Block::out` unsolved made `setne %al; movzbl %al, %r13d`
  into `mov %rax, %r13` with `%r13d` read three blocks on - `dynamic-cast`
  at both levels, caught by the suite runs while every other check was
  green. `f_.live(s_)` at the top of the walk.
- **The `lea`'s 64-bit base kept a sign extension alive** that the 32-bit
  `add` had let die: chasing copies did not reach the source (a later
  `mov $97, %rax` had bumped the version), the value model did
  (`sextFrom_`: the extension's number to the unknown it extends, and
  `holding()` of that).

## Measured here

| Check | S9a | S9b | S9c |
|---|---|---|---|
| `bash tests/run.sh` | 477 / 0 | 478 / 0 | 479 / 0 |
| every case at -O1 and -O2, host, run and diffed | 288 / 0 each | 289 / 0 each | 290 / 0 each |
| `tests/emit.sh` (the -O0 golden at 5e6e2e3) | 0 of 839 changed, 3 added | 0 of 839, 6 added | 0 of 839, 9 added |
| `tests/overload.sh`, `tests/names.sh` | 30 / 0, 288 / 0 | 30 / 0, 289 / 0 | 30 / 0, 290 / 0 |
| `tools/identical.sh`, `LEVELS=0`, against e91a4f6 | 576 / 0 differ | 578 / 0 | 580 / 0 |
| `tools/comment-lines --count` | 63 | 63 | 63 |
| the MASM spelling, every case at -O1 and -O2, emitted | 532 / 0 refused | 534 / 0 | 536 / 0 |

The size proxy over the 288 objects session 7 measured (the two new
cases, `hoist-invariants` and `forward-values-narrow`, subtracted):

| | e91a4f6 | S9a | S9b | S9c |
|---|---|---|---|---|
| linux -O1 | 317,921 | 317,921 | 317,921 | 317,534 |
| linux -O2 | 390,681 | 378,428 | 376,956 | 376,075 |
| windows -O1 | 318,226 | 318,226 | 318,226 | 317,852 |
| windows -O2 | 388,612 | 387,573 | 385,873 | 385,122 |

-O1 does not inline and does not hoist, so only S9c's rewrites reach it.

## The speed table

`tools/windows/bench-kernels.cpp`, x86_64-linux -O2, 11 interleaved
rounds, medians in ms, checksums equal throughout; g++ 13 and clang 18 at
-O2 -std=c++11, an Intel Xeon at 2.1 GHz. "-h" is S8's placement control:
every function entry and loop head at `.p2align 4`, a non-PIE link, on
both sides. One final run of all ten binaries together:

| kernel | ref | S9a | S9b | S9c | g++ | clang | ref-h | S9a-h | S9b-h | S9c-h |
|---|---|---|---|---|---|---|---|---|---|---|
| fib | 13 | 11 | 11 | 11 | 3 | 0 | 14 | 9 | 9 | 9 |
| sieve | 73 | 74 | 72 | 71 | 63 | 64 | 81 | 76 | 74 | 72 |
| matmul | 39 | 40 | 20 | 20 | 10 | 5 | 40 | 38 | 19 | 19 |
| isort | 25 | 25 | 17 | 17 | 9 | 15 | 25 | 25 | 17 | 18 |
| hash | 293 | 289 | 288 | 279 | 217 | 156 | 296 | 291 | 288 | 275 |
| virtual | 200 | 197 | 182 | 205 | 154 | 154 | 146 | 189 | 202 | 218 |
| total | 650 | 647 | 613 | 615 | 461 | 404 | 606 | 635 | 612 | 613 |

Ratio of the total to g++'s: 1.41 at e91a4f6, **1.33** at S9c; to clang's
1.61 to 1.52. Without `virtual`, whose loop never changed: 450 -> 410 ms
against g++'s 307 and clang's 250, 1.47x to 1.34x. Per kernel at S9c
against g++: fib 3.7x (was 4.3), sieve 1.13x, matmul 2.0x (was 3.9),
isort 1.9x (was 2.8), hash 1.29x (was 1.35), virtual level under the
control.

Each step's own commit carries the run that gated it; the rows above are
one run of all ten binaries together, so the columns disagree with the
commits by the noise (S9c's commit measured hash 288 -> 279 against S9b's
build the same way).

## What the box must run to judge it

From `C:\cxxopt`, as sessions 3 to 7 laid it out:

1. `sync.cmd` with a bundle of this branch's head (`906a19c` plus the docs
   commit), then `msvc\build.cmd`.
2. `tools\windows\bench.cmd -New ... -Rounds 11` against cl /O2 and cl6x:
   fib, matmul, isort and hash are the rows to expect movement in.
3. `opt.cmd s9 O1 O2`: Compiler++ at both levels, its four suites, the
   258 cases against `C:\cxx1dev\w\cl-O2`. The gate: 258/258 and green.
   **This is the first time a recursive function is walked into itself,
   an inlined callee's locals are in registers, a value is hoisted in
   front of a loop, and a byte extension is a copy, under a Windows
   CRT**; the hoist pass chooses registers by the Microsoft convention's
   liveness and has only ever run under the SysV one here.
4. `cases.cmd O0 O1 O2`: cxx1's cases on the box, `hoist-invariants` and
   `forward-values-narrow` among them, through both assembly spellings.
5. `ab.cmd 15 base-O2 s9-O2` and `ab.cmd 15 base-O1 s9-O1`.
6. `.text` of Compiler++ at -O1 and -O2, GNU and MASM routes: the proxy
   says -O2 falls about 3.7% and -O1 0.1%.

## Next, in order

- **hash at 1.29x** is the row that says most now: the fill loop is 19
  instructions where g++'s is 11. The two moves the `imul3` form removes
  did not pay here; what is left is `r + i` as an induction variable
  (g++'s `leal (%rsi,%rcx)`), the index sign-extended every turn
  (`movslq %eax, %r8` - a 64-bit induction variable), and the signed
  division's sign correction (`shr $63; add`), which value-range
  knowledge that `r + i` is non-negative would drop. The byte loop is
  latency-bound on `imul`+`xor` and is the same for every compiler.
- **matmul at 2.0x**: the `x` reload from its slot every turn (an xmm
  slot's load is hoistable when nothing in the loop stores to the frame
  and the slot is never addressed - the pass has the loop, not the xmm
  palette) and the three `movapd` around `mulsd`/`addsd` - an xmm
  coalesce, session 6's item. The k loop's four instructions want a
  preserved register with a save (`Function::saves`, `insertRestores`,
  as the allocator's `addSaves` does), which the pass does not take.
- **isort at 1.9x**: `movl (%r10,%r8,4)` loaded twice across the `jle`,
  and `movslq %eax, %r8` twice - a dominator-based redundant load and
  extension removal, `Flow::dominators()`' next client.
- **fib at 3.7x**: the second level of recursive inlining, in the walker.
- **A 32-bit copy the allocator will not coalesce** (`mov %r9d, %edi`
  before `imul $26, %edi`): the copies it collects are eight-byte ones,
  because a four-byte copy zero-extends; where the destination web is
  never read wide it is a plain copy. Needs "read wide" liveness over the
  pseudos.
- Session 7's list stands: the unsigned divide's "add" case, the Windows
  `shl $3, %eax; movslq`, a temporary across a call at -O1, Z1's cost.
