# Handover T1: the C6000 optimizer's first round, 2026-09-25

The tms6747 backend now takes `-O1` and `-O2`, and both run the same four
passes over each function's parameter and body text in
`src/optimizer/C6xSched.cpp` - the prologue and epilogue hold no padding and
are decided after the body, so they stay outside it. `-O0` output is
byte-identical to before.

**The model.** The -O0 code is serial and padded to every latency, so with its
NOPs removed it is a sequential program. The passes rewrite that program and
the scheduler pads it again: a read waits for its pending write to land, a
write lands after the pending one it replaces, a branch waits until every
pending write lands by the time its target executes (issue + 6), a directive
is a barrier, and a label needs no wait - a jump arrives with nothing in
flight, and the fall-through path is scheduled as the sequence it is. The
latencies are the emulator's, taken from the -O0 backend's own NOP counts:
LD* 4, MPY* 3, MPYSP/ADDSP/SUBSP 3, ADDDP/SUBDP 6, MPYDP 9, INTSP 3, INTDP 4,
SPTRUNC/DPTRUNC 3, the FP compares and SPDP/DPSP 1, B 5.

**The passes**, in order: `MVKL v; MVKH v` of a constant in sixteen signed
bits becomes `MVK v`; a local's address built and used once becomes
`*-A15(k)` where k fits ucst5 scaled by the access and the address register
and A0 are dead after; a push whose pop is in the same block, with no branch,
call or other use of B15 between, keeps its value in A16-A31 (slot d takes
A16+2d, and A17+2d for a pair) with both stack adjustments dropped.

**Measured** on `tools/bench-c6x.cpp` (kilocycles from the emulator's
`clock()`, checksum `6765 4524 64744 344346 3658597632 290000` at every
level):

| kernel | -O0 | re-pad only | -O1 = -O2 | gain |
|---|---|---|---|---|
| fib | 2189 | 2046 | 1751 | 20% |
| sieve | 20626 | 19263 | 14380 | 30% |
| matmul | 5224 | 4780 | 3366 | 36% |
| isort | 19812 | 18209 | 12651 | 36% |
| hash | 39818 | 36636 | 26404 | 34% |
| virtual | 6001 | 5421 | 4141 | 31% |
| total | 93670 | 86355 | 62693 | 33% |

`tests/tms6747.sh` takes `CXX1_FLAGS` and reads 325 passed / 0 failed at
-O0, -O1 and -O2; run.sh 532/0; the host emit golden 0 of 1305 changed;
`make comments` 63, as at HEAD.

**What the pricing said.** In the -O1 bench text 198 pushes became register
moves (55 remain, each with a call or a label between it and its pop) and
only 30 of 281 local addresses folded into `*-A15(k)`: the stack machine's
frames are large, most locals sit past 124 bytes, and those keep
`MVK k, A0; SUB A15, A0, R` - two cycles where three were. The hash inner
loop went from 128 cycles per turn to about 90.

**One fault the corpus caught and the bench did not.** `accessSize` took any
mnemonic ending in `DW` for an 8-byte access, so `LDW` scaled ucst5 by 8 and
`*-A15(136)` reached the emulator; four cases failed at -O1 while the bench
ran clean. `LDDW`/`STDW` are named now.

**T2.** The next cycles are in what the peepholes leave: `MV A4, A16 ...
MV A16, A4` pairs where the value is never disturbed (copy propagation over
the sequential model, then dead moves); `MVK k, A0; SUB A15, A0, R` for the
frames past 124 bytes, which `*-A15[A0]` register-offset addressing or an
`ADDK` on a copy of A15 could shorten; and the branch delay slots, which are
five NOPs at every `B` and are the largest padding left - a scheduler that
moves independent instructions into them, or `BNOP`, is the round after that.
A load whose result is popped straight back (`STW A4,*B15 ... LDW *B15,A4`)
still costs a memory round trip where a push crosses a call.
