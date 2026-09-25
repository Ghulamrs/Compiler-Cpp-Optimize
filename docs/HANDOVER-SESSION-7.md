# Handover to session 8: S8, instruction selection in the hot loops

Fable 5.1, 2026-09-24, end of session 7 on branch
`claude/fable-5-1-background-work-kguwml`, from `53e0d28` (session 6's
head). A Linux container: no Mac, no Windows box, no cl, no cl6x, no
Compiler++ tree. Everything below was measured here; the last section
says what the box has to run.

Read first: `docs/OPTIMIZER-ARCHITECTURE.md` (kept in step: the memory
operand's index, `fold-index`, `thread-jumps`, `divide-by-constant`, the
pipeline declaration), then `docs/HANDOVER-SESSION-6.md` for S7 and the
numbers this session started from.

## Why this session was different

S5 to S7 made the code 7-14% smaller and not faster: at 53e0d28 the
kernels' total was 1.63x g++'s. S8's gate was speed - a step lands only if
the benchmark is faster in a fair comparison and never slower - and it was
approached by reading each kernel's -O2 assembly next to g++'s and pricing
each candidate **by editing the emitted assembly by hand and timing it
before writing a pass**. Three of the five candidates paid and were built;
two did not and were not.

## What landed, in order

| Commit | Step | What it is |
|---|---|---|
| `a2c13f3` | S8a | `accessWidth` answers 8 for `movsd` and 4 for `movss` instead of "cannot tell" (16): a double stored beside a loop counter no longer keeps the counter out of a register. matmul's `k` and `j` were in memory for this alone; session 6's handover had guessed fold-loads |
| `934447e` | S8b | The memory operand carries an index register and a scale; `fold-index` (after `frame`) folds `add %idx, %base` into `(%base,%idx,1)` and a `shl $k`/`imul $s` of the index into the scale; forward-values folds a frame address into an indexed base and a constant through a narrowing move (`movb $1, (..)`) |
| `803a17c` | S8c | `thread-jumps` (last in `rounds`): a jump to a block that only jumps on, and a constant carried into a compare-and-branch deciding it - the `&&` chain's 0/1 no longer materialised and re-tested; the walker registers `sc`/`scend` as jump-only |
| `5e0a2a0` | S8d | `divide-by-constant` (last in `rounds`, -O2 only): a 32-bit `idiv`/`div` by a constant as a multiply by its magic number; `tests/cases/divide-by-constant.cpp`, 38 divisors at the boundaries, clang, g++ and cxx1 agreeing at every level |

### Priced by hand and not landed

- **Loop rotation** (the header's compare duplicated at the latch, one
  taken jump per turn instead of a not-taken plus a taken): sieve 76 to 75,
  matmul and hash unchanged, and it grows code. The branch unit takes the
  walker's `jcc; ...; jmp` shape at no measurable cost on this machine.
- **The unsigned divide's "add" case** (7, 641, 100000, 1000003, 2^31-1
  have no exact multiplier below 2^32): left as `div`. The 64-bit `mul`
  form is the way in if a program needs it; nothing in the kernels does.

### What each cost beyond the obvious

- **The reason, not the guess.** Session 6 said matmul's counters stayed in
  memory because fold-loads' folded `cmp` is not renamable. Instrumenting
  `promotableLocals` said `movsd %xmm0, -24(%rbp)` at width 16 covering
  `-12`. One line; matmul 57 to 41.
- **Where the index pass sits.** After `frame`, so webs and the allocator
  never meet an indexed operand (webs asserts it) - the alternative was
  teaching occurrence tracking a second register per operand. It runs
  before `finish-frame`, where `imul $8` is still a multiply (`shrink`
  makes the shift later), so both spellings of a scale are matched.
- **rbp is never a base or index for the pass**, so every `reg.id == RBP`
  test on a slot stays exact; the one indexed rbp form, forward-values
  folding a frame address into an indexed base (`-64(%rbp,%rcx,8)`), needed
  `scale == 0` at four sites in forward-values, at `frameSlot`, and an
  "unknown reach" in dead-store removal.
- **Two misses the first version of fold-index had**, both read off the
  emission: an index rewritten *after* the base's last use refused the fold
  (matmul's second address), and the index's own whole redefinition was
  taken for a value read (`movslq %esi, %r8` ending it) - the operand's
  role, not its mention, says which.
- **The Windows shape `shl $3, %eax; movslq`** - the parser scales in a
  32-bit `long` there - is not folded: a sign extension does not commute
  with the shift. A Windows-only miss, recorded.
- **thread-jumps' insertions are applied highest index first**; the first
  version applied them in push order and a label landed between a `cmp`
  and its `je`. The checksum did not see it (the flags happened to agree);
  reading the emission did.
- **A label the pass makes must be droppable.** Only the return label was
  jump-only before; `sc` and `scend` are now, which is what lets the pair
  and its code go. No other label kind gained the status, so a
  compare-and-branch threaded past elsewhere leaves its two instructions
  standing (bytes, never behaviour).
- **The division case's first draft was wrong, not the pass**: `m * D`
  overflowed `int` for D near 2^31, and clang folded the undefined product
  differently. Then two names-suite traps in a row - a local array's
  initialiser is a `memcpy` for clang, and a folded `const int` has no
  symbol - and one function of 500 divisions took 25 s to compile for
  Windows at -O2 under the pre-existing round cost, which the size proxy's
  20 s limit swallowed silently as a missing object. One function per
  divisor: 0.1 s.

## Measured here

| Check | S8a | S8b | S8c | S8d |
|---|---|---|---|---|
| `bash tests/run.sh` | 476 / 0 | 476 / 0 | 476 / 0 | 477 / 0 |
| every case at -O1 and -O2, host, run and diffed | 287 / 0 each | 287 / 0 each | 287 / 0 each | 288 / 0 each |
| `tests/emit.sh` (the -O0 golden at 5e6e2e3) | 0 of 839 changed | 0 of 839 | 0 of 839 | 0 of 839, 3 added |
| `tests/overload.sh`, `tests/names.sh` | 30 / 0, 287 / 0 | 30 / 0, 287 / 0 | 30 / 0, 287 / 0 | 30 / 0, 288 / 0 |
| `tools/identical.sh`, `LEVELS=0`, against 53e0d28 | 574 / 0 differ | 574 / 0 | 574 / 0 | 576 / 0 |
| `tools/comment-lines --count` | 63 | 63 | 63 | 63 |
| the MASM spelling, every case at -O1 and -O2, emitted | - | 530 / 0 refused | 530 / 0 | 532 / 0 |

The size proxy (every case at a level for x86_64-linux and x86_64-windows,
assembled by clang, `.text` summed; the last column over the 288 cases
that include the new one, its S8c value re-measured over the same set):

| | 53e0d28 | S8a | S8b | S8c | S8c (288) | S8d (288) |
|---|---|---|---|---|---|---|
| linux -O1 | 313,464 | 313,316 | 308,156 | 306,242 | 317,921 | 317,921 |
| linux -O2 | 378,739 | 378,491 | 372,436 | 370,353 | 386,897 | 390,681 |
| windows -O1 | 310,430 | 310,385 | 307,117 | 305,412 | 318,226 | 318,226 |
| windows -O2 | 370,145 | 370,076 | 366,277 | 364,373 | 383,976 | 388,612 |

S8d's growth is the new case: 3,623 of the 3,784 linux bytes, the other
eight objects that moved 161 bytes together.

## The speed table

`tools/windows/bench-kernels.cpp`, x86_64-linux -O2, 11 interleaved rounds,
medians in ms, checksums equal throughout; g++ 13 and clang 18 at -O2
-std=c++11. "-h" is the placement control: every function entry and loop
head (`.begin.N`) at `.p2align 4`, a non-PIE link, on both sides. (Aligning
*every* label, as session 6 did, pads inside the loops and is a different
program; aligning functions alone leaves the loop head where the previous
function's length puts it.) `virtual` moves 143 to 198 between builds of
one unchanged loop body under every control and is a placement lottery.

| kernel | ref | S8a | S8b | S8c | S8d | g++ | clang | ref-h | S8a-h | S8b-h | S8c-h | S8d-h |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| fib | 13 | 13 | 13 | 13 | 13 | 3 | 0 | 13 | 13 | 13 | 13 | 14 |
| sieve | 92 | 90 | 74 | 75 | 72 | 58 | 61 | 80 | 81 | 76 | 75 | 80 |
| matmul | 51 | 39 | 39 | 40 | 39 | 9 | 4 | 51 | 41 | 38 | 38 | 39 |
| isort | 36 | 40 | 31 | 25 | 25 | 9 | 14 | 36 | 36 | 31 | 25 | 25 |
| hash | 331 | 329 | 325 | 325 | 291 | 208 | 157 | 331 | 331 | 322 | 326 | 287 |
| virtual | 197 | 154 | 167 | 143 | 154 | 153 | 150 | 153 | 198 | 144 | 157 | 148 |
| total | 731 | 670 | 649 | 623 | 609 | 445 | 391 | 672 | 711 | 628 | 640 | 597 |

Ratio of the total to g++'s: 1.64 at 53e0d28, **1.37** at S8d (1.34 with
the heads aligned); to clang's, 1.87 to 1.56. Per kernel at S8d against
g++: fib 4.3x (the recursion, not selection), sieve 1.24x, matmul 4.3x,
isort 2.8x, hash 1.40x, virtual level.

Each step's own commit carries the run that gated it; the rows above are
one final run of all five binaries together.

## What the box must run to judge it

From `C:\cxxopt`, as sessions 3 to 6 laid it out:

1. `sync.cmd` with a bundle of this branch's head (`5e0a2a0` plus the docs
   commit), then `msvc\build.cmd`.
2. `tools\windows\bench.cmd -New ... -Rounds 11` against cl /O2 and cl6x:
   matmul, sieve, isort and hash are the rows to expect movement in.
3. `opt.cmd s8 O1 O2`: Compiler++ at both levels, its four suites, the
   258 cases against `C:\cxx1dev\w\cl-O2`. The gate: 258/258 and green.
   **This is the first time an indexed operand, a threaded jump and a
   magic-number divide run under a Windows CRT**; the MASM rendering
   `[base+index*scale+d]` has only been emitted here, never assembled -
   ml64 is what says it is right.
4. `cases.cmd O0 O1 O2`: cxx1's cases on the box, `divide-by-constant`
   among them, through both assembly spellings.
5. `ab.cmd 15 base-O2 s8-O2` and `ab.cmd 15 base-O1 s8-O1`.
6. `.text` of Compiler++ at -O1 and -O2, GNU and MASM routes: the proxy
   says -O1 falls 2.3% and -O2 about 1% net of the divide sequences.

## Next, in order

- **matmul at 4.3x g++** is now the row that says most: three row
  addresses recomputed per turn (`movslq; imul $1280; lea; add` twice for
  `mc[i]`) want value numbering that keeps a value in a *different*
  register across the clobber - CSE before webs, as pseudos; and the three
  `movapd` around the `mulsd` with `x` reloaded from `-32(%rbp)` every turn
  are the xmm palette the allocator does not have (session 6's item).
- **hash at 1.40x**: the byte loop loads `(%rdx)` twice across the `je`
  (the header's block and the body's) - a dominator-based redundant-load
  removal, which is `Flow::dominators()`' second client.
- **sieve at 1.24x**: `movslq %edx, %rdi` of the loop-invariant bound
  every turn, and `lea comp(%rip)` inside the loop - loop-invariant code
  motion, which `Loops` can already name the blocks for.
- The unsigned divide's "add" case through a 64-bit `mul`, if a program
  needs it; and `shl $3, %eax; movslq` on Windows, which is the parser's
  32-bit scaling and not the backend's.
- Session 6's list stands: a temporary across a call at -O1 charged in
  bytes, the inliner's callees with a `switch`, Z1's cost.
