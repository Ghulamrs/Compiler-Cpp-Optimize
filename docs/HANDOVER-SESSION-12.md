# Handover to session 13: S12 #2, the three xmm gaps priced apart, two built

Fable 5.1, 2026-09-25, from `c89e611`. A Linux container: no Mac, no
Windows box, no cl. Read `docs/HANDOVER-SESSION-8.md` for the gate and
`docs/HANDOVER-SESSION-11.md` for S12 #1, which priced the three together.

## The price of each gap alone, by hand-editing the -O2 assembly

`tools/windows/bench-kernels.cpp`, x86_64-linux -O2 from `c89e611`, the
inner loop of `matmul` edited one gap at a time and in every combination,
assembled with `c++ -no-pie`, 11 interleaved rounds, medians with the
middle-half range, checksums equal. (a) is the four `movapd` shuffles, (b)
the two recomputed `movslq %eax, %r9`, (c) the `addsd mem, %xmm` fold:

| edit | loop | matmul | other kernels |
|---|---|---|---|
| ref, as emitted | 13 | 19 (18-20) | |
| a | 9 | 19 (18-20) | within range |
| **b** | 11 | **17 (16-17)** | within range |
| c | 12 | 18 (17-20) | within range |
| **a + b** | 7 | **14 (14-16)** | within range |
| a + c | 8 | 16 (15-17) | |
| b + c | 10 | 16 (15-16) | |
| a + b + c | 6 | 14 (14-16) | |
| g++ -O2 | | 10 (9-10) | |
| clang -O2 | | 4 (4-4) | |

(b) alone carries 10% and is the only gap over the 5% floor on its own;
(a) alone is worth nothing and with (b) is worth 26%, which is the shape
the S11 hash pricing had too - a throughput-bound loop pays for a removed
uop only once the port it competed for is the bottleneck. (c) adds nothing
over (a)+(b).

## What was built

- **(b) was one line, and not what the handover said.** Value numbering
  already tracked the extension (`sextOf_`); `extensionHeld` asked which
  *other* register holds it, so a `movslq` whose destination already did
  was neither a copy nor a no-op. `isNoop` answers it now.
- **(a) is three rewrites in `coalesce-copies`**, each on a copy whose
  target is dead after: the GPR `pure` retarget widened to an xmm whole
  write followed by `movapd`; `movapd %s,%d; op %d,%x` to `op %s,%x`;
  and `movapd %s,%d; movsd mem,%s; addsd %d,%s` to `addsd mem,%s` for
  `addsd` and `mulsd`, which is the (c) fold in the one shape the stack
  discipline makes. A frame slot is excluded from that last one: folded,
  the locals pass stopped promoting the *function's* counters, and matmul
  came out with every index reloaded from `-36(%rbp)`.
- The MASM rules for `addsd subsd mulsd divsd` carry width 8 so a memory
  operand is spelled `QWORD PTR`; no emission reaches it yet.

The loop is `movslq; movsd mem,%xmm2; movsd slot,%xmm0; mulsd; addsd mem,
%xmm0; movsd`, the hand-cleaned form of S12 #1 plus the slot load.

## Measured, final build, 11 interleaved rounds

| kernel | ref `6826941` | S12 | g++ -O2 | clang -O2 |
|---|---|---|---|---|
| fib | 10 (10-11) | 10 (10-11) | 3 | 0 |
| sieve | 73 (71-76) | 72 (71-76) | 58 | 62 |
| matmul | 18 (18-19) | **15 (14-15)** | 10 | 4 |
| isort | 17 (17-18) | 17 (17-17) | 9 | 14 |
| hash | 266 (264-278) | 269 (263-275) | 213 | 154 |
| virtual | 143 (143-156) | 148 (143-157) | 153 | 152 |
| total | 536 (531-551) | 540 (522-551) | 449 | 389 |

hash and virtual are within their middle-half ranges and their functions
are byte-identical between the two builds (`_Z4virti`, `_Z6hashesi`,
`_Z5sievei` diffed): S9's placement effect, not the change.

Gate: clean make; the two touched optimizer files clean under `clang++
-std=c++14 -Wall -Wextra -Werror -pedantic -fsyntax-only`; `run.sh` 480/0;
every case at -O1 and -O2 run and diffed 582/0; `emit.sh` 0 of 839 changed
(12 added, the golden being older than the last two cases); `identical.sh`
`LEVELS=0` against `cxx1-6826941` 582 compared, 0 differ; the MASM
spelling 538/0 at -O1/-O2 and its -O0 output identical to the reference on
all 269 cases; `make comments` 63. Size proxy at -O2: linux 557,585 ->
556,923 (-0.12%), windows 396,904 -> 396,386; -O1 linux 486,760 -> 486,240,
windows 321,186 -> 320,787. 41 of 290 linux -O2 emissions changed.

## What is left

- The Windows shape of the same loop is `movslq; shl $3; movslq` per index,
  so neither (b) nor the `addsd mem` fold fires there; the `shl` between
  the two extensions is what `fold-index` leaves on that target.
- The slot load of `x` stays in the loop (S12 #1: hoisting it is worth 0).
- S11's divide-by-constant sign correction, still needing a proof.
- The reference for the next round is this commit's `cxx1.exe`.
