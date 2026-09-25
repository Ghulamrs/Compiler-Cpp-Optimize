# Handover to session 11: S11, the hash kernel, priced and not landed

Fable 5.1, 2026-09-25, from `6826941`. A Linux container: no Mac, no
Windows box, no cl, no Compiler++. Read `docs/HANDOVER-SESSION-8.md` for
the gate and `docs/HANDOVER-SESSION-9.md` for S10.

## What was priced, by hand-editing the reference's -O2 assembly

The fill loop of `hashes` (`buf[i] = 'a' + (r + i) % 26`) is 19
instructions against g++'s 11, and at 126M turns it runs at about 4.75
cycles a turn - throughput-bound, not latency-bound, so what costs is an
instruction that is not a renamed move. Five edits of `_Z6hashesi`, each
assembled with clang and linked `-no-pie`, 7 interleaved rounds, medians
with the middle-half range, checksums equal:

| edit | hash | what it removes |
|---|---|---|
| ref (`cxx1-6826941`) | 266-275 | |
| A: `mov; add` -> `lea (%rcx,%rax)` | 266-268 | one move: nothing |
| D: `add %r,%eax; mov %eax,%r` -> `add %eax,%r` (quotient dead) | 270 | one move: nothing |
| **B: the sign correction dropped** (`shr $63; add; mov`) | **240-253** | one dependent `add`: 10-12% |
| R: both inner loops rotated (test at the bottom, no `jmp`) | 265-267 | a taken branch: 3%, in the noise |
| R + B | 242 | no more than B |
| g++ -O2 | 209-217 | |
| clang -O2 | 154-158 | |

Every edit that shortens `hashes` moves `virt` (145 -> 155-158): padding
the function back to its 476 bytes with `.skip` restores 141-148, so that
is S9's placement effect and not the edit. The other kernels do not move.

## Why B is not built

Dropping the correction needs the dividend `r + i` known non-negative,
which g++ takes from signed-overflow UB. The stream cannot: an `add` does
not say whether the source's addition was signed, and
`(int)((unsigned)r + (unsigned)i)` is well-defined C++ that wraps
negative for large `r` - a program cxx1 must still divide correctly. The
counters alone are provable at machine level (a slot set to a
non-negative immediate, stepped by `add $1` only, exited by a signed
compare), but their sum is not bounded by anything the stream holds.

What it takes, and it is a round rather than a peephole:

1. The walker marks the `add` (and `sub`, `imul`) it emits for a signed
   integer `Binary` - a flag on `Instr` set through a `Spelling` hook,
   carried by `fold-loads` and `forward-values` when they rewrite it.
2. A counter analysis over `Loops` at the first `rounds`, where the
   counters are still frame slots (`-88(%rbp)`, `-92(%rbp)` in the dump):
   initialised to a non-negative immediate in the preheader, every write
   inside the loop a `+1`, no live `lea` of the slot anywhere.
3. At the divide site, a backward trace from `cdq` through `movslq`, `mov`
   and marked `add`s to slots the analysis vouches for; then the sign
   correction is left out and the quotient's `add` becomes a move.

## What the Windows box has to run

Nothing new: the tree is `6826941` plus these two documents.

## Next

- The above, if hash's 12% is wanted; then g++'s remaining gap on this
  kernel is the 64-bit induction variable (`movslq %eax` each turn), which
  the same counter analysis answers.
- Loop rotation is worth 3% here and is within noise; it is a
  `shrink`-sized pass and cheap to price on sieve and isort, whose loops
  are shorter.
- S10's list stands: the two-callee effect at the indirect call,
  `align-entry` capped at half a line on a machine without it.
