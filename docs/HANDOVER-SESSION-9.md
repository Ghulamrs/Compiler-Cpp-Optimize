# Handover to session 10: S10, the small callee, priced and not landed

Fable 5.1, 2026-09-25, from `e1c8454` (S5-S9 and `src/optimizer/` merged).
A Linux container: no Mac, no Windows box, no cl, no cl6x, no Compiler++
tree. Read `docs/HANDOVER-SESSION-8.md` first for S9 and the gate.

## The one candidate, and what the measurement said

S9's next candidate was a small callee at a 64-byte line start (the
virtual-call kernel's two `area()` methods, 145 -> 125 ms by hand). Built as
`align-entry`, last in the -O2 pipeline: a whole function whose estimated
bytes (OptAlign's estimate, plus 13 for the prologue and epilogue events and
8 per callee save) fit a line gets `Spelling::entryAlign(L)`, written by
`functionBegin` after its section directive as the same `.p2align 6,,L-1` a
loop head gets; MASM ignores it. To put the pad in front of the label the
`Optimizer` holds `functionBegin` as the stream's first event, and tells the
spelling the name at once through a new `functionNamed` hook, because MASM
names funclets and their sections after the current function *while* the
stream is held (without the hook, `main$catch$0` came out under the previous
function's name; found by `tools/identical.sh` at -O1, 77 MASM files).

**What is true.** With the caller's loop pinned at one line offset and the
callees moved in 8-byte steps (`.skip` before each, the compensating `.skip`
before `_Z4virti`, virt's loops unchanged), a 23-byte callee that straddles
a line costs 153-159 ms against 144-147 not straddling; `.p2align 6,,22` on
the straddling one gives 149 (A/B, 9 rounds, 154 -> 149). The rule recovers
what a straddle costs, and the estimate errs long (31 for 23 bytes).

**What decides the kernel is something else.** Sq at offset 0 and Rc at 2
read 187; Sq at 2 and Rc at 0 read 196; both at 0 read 122 (119-124 over 11
rounds); every other non-straddling pair 144-147. A two-callee effect at
the indirect call, not a line rule, and no per-function pad reaches it. So
the gate flipped on placement alone: uncapped (every function <= 64 bytes,
2540 pads over 291 cases, -O2 `.text` +7.9% linux / +2.6% windows) the
callees landed at 0x1840 and 0x18c0 and the total read 510 against 548;
capped at half a line (1780 pads, +1.7% / +0.3%) they landed at 0x1802 and
0x1880 and it read 585 against 527. Neither number is the pass's own.

| kernel | ref | uncapped | capped | g++ | clang |
|---|---|---|---|---|---|
| fib | 10 | 10 | 10 | 3 | 0 |
| sieve | 71 / 70 | 71 | 70 | 58 | 61 |
| matmul | 19 / 18 | 18 | 18 | 9 | 4 |
| isort | 17 | 17 | 17 | 8-9 | 14 |
| hash | 270 / 261 | 267 | 270 | 214 / 211 | 154 |
| virtual | 145 | **122** | **196** | 157 / 150 | 153 / 152 |
| total | 548 / 527 | 510 | 585 | 453 / 443 | 389 / 387 |

(11 interleaved rounds each, medians; ref, g++ and clang were run in both
sessions and both readings are given; checksums equal throughout.)

**Not landed.** A rule whose gate result is decided by where two callees
happen to fall is not a rule the gate can pass, and 7.9% of `.text` is not a
trivial price for the half of it that is real. The straddle half is worth
6-8% of this kernel when it applies; it is written down here, with the
shape, for a machine or a benchmark where the two-target effect is absent.
The source is reverted; every check on the built tree was green before the
decision: run.sh 480/0, -O1 and -O2 over 582 runnable cases 0 failed,
`identical.sh` at -O0 and -O1 1455 compared 0 differ (after the MASM hook),
emit.sh 0 of 839 changed, `make comments` 63 (the base).

## What the Windows box has to run

Nothing new: the tree is `e1c8454` plus this document. The S9 handover's
list stands.

## Next

- The two-target effect on the indirect call: measure it with three and four
  callees, and with the callees far apart (0x1000), before designing for it;
  if it is a BTB set conflict, only a linker-level placement could help.
- `align-entry` capped at half a line, on a machine that does not show the
  effect: the shape above, one commit.
