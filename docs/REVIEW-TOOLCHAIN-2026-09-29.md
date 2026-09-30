# The C6747 toolchain reviewed: cpp11 (tms6747), ASM6x and LNK6x - 2026-09-29

Three Fable 5.1 reviews, read-only, with TI's CGT 7.4.4 (CCS 5.5) and 8.2.2 (CCS 7.4) on the
Windows box as the oracle and VM6747's new cycle counter and profile for speed. The three full
reports follow this summary.

## Where the gap is

- 73% of cpp11's cycles on TI's simulator over the six kernels are L1D write-buffer stalls
  (203 M total against 55.8 M CPU): the stack machine stores and reloads every local, parameter
  and push. Whatever removes memory traffic is worth about 3x what vm6747 -c reports.
- cpp11 -O1 .text is 3.94x cl6x -ms3 (8,096 against 2,056 bytes over the kernels); 15% of it
  is NOP words, and cl6x's compaction (16-bit instructions) takes another 14-20% off the same code.

## Must fix - wrong answers

| | where | what |
| --- | --- | --- |
| L-A1 | LNK6x | every .c6xabi.exidx LNK6x writes is zeros: no C++ exception can be caught in an LNK6x image (one condition in elf.cpp; then sort, and synthesise EXIDX_CANTUNWIND) |
| L-A2 | LNK6x | rle24 .cinit is corrupt when a section uses all 256 byte values; runs >= 256 not lnk6x's |
| L-A3 | LNK6x | R_C6000_EHTYPE refused - the Compiler++ harness cannot link (it is S+A) |
| L-A5 | LNK6x | out-of-range relocations truncated silently (a call 16 MB away links rc=0) |
| C-A5 | cpp11 | functions past 64 KB get pr3 unwind tables with 16-bit scopes - silently wrong; cl6x uses pr2 |
| C-A1 | cpp11/VM6747 | no rule for the C67x two-cycle DP source read - safe only while nothing is packed in parallel |

## Speed (-O2), in the reviewers' order, with their estimates

1. cpp11 B1: foldFrame never fires on a local - emit `*-A15(k)` directly: -9 to -14% CPU, -12% size
2. cpp11 B4 + B3: branch on the compare itself; MPY32 by constants and scst5 immediates; divide by
   a constant by multiply (hash's 126,000 `__c6xabi_remi` calls): -10 to -15% on loops
3. cpp11 B5(1) + B7: BNOP / CALLP / RETNOP and a lean prologue: fib about -25%, size -25%
4. cpp11 B2: locals in registers (A10-A14, B10-B13; A16-A31 in leaves): sieve -30%; kernels to ~4x
5. cpp11 B8, B9: reuse frame slots; pipeline copyBlock
6. cpp11 B5(2), B6: fill delay slots; parallel packets and software pipelining (a week)
7. ASM6x B1: compaction - 14% (-O2) to 20% (corpus) of .text

## Tools and accuracy

- ASM6x is table-identical to CGT 8.2.2 on all 347 corpus objects and the Compiler++ harness;
  byte identity fails for three cosmetic reasons (local symbol order, sh_entsize, e_phentsize).
- ASM6x and LNK6x each spend 75-85% of their time in a linear symbol or string search: a hash
  takes ASM6x from 1.9 s to 0.56 s and LNK6x from 2.85 s to 0.022 s on the harness, output
  byte-identical.
- LNK6x layout rules: descending size (not .bss first), .stack's flags and tie with .sysmem,
  positional options, .TI.symbol.alias ignored (a dead 32-byte stub kept).

## Measurement

- VM6747: a per-native cost table and a first-order L1D stall model fitted to TI's totals
  would make vm6747 predict TI's cycle.Total, not only cycle.CPU.
- Gate tests/tms6747.sh CYCLES=1 and bytes against a recorded baseline; record a -O2 emit golden
  for tms6747; add a C++ throw probe and a run of an LNK6x image on TI's simulator.


---

# cpp11 on the TMS320C6747: a read-only review of the code generator, the scheduler, the inliner and the library's cost

Reviewed 2026-09-29 on branch `tms-opt` at `f5420e5` (working tree, `cpp11.exe` of 01:06), read-only.
Files: `src/backend/Tms6747.{h,cpp}` (1,642 lines), `src/optimizer/C6xSched.{h,cpp}` (439),
`src/optimizer/Inliner.h`, `include/string`, the parser's frame allocation, and the three prior
reviews in `VM6747/TMS6747-REVIEW.md` plus `docs/HANDOVER-C6747-2026-09-29.md`.
Oracles used: `vm6747.exe -c/-p` (TI cycle.CPU within 0.2%), `cl6x` 7.4.4 and 8.2.2 on the Windows
box (`C:\cxx1\review\`: `-O2 -k`, `-O2 -ms3 -k`, `-O1 -k`, all `--symdebug:none`), `ofd6x` for
section sizes. No TI simulator run, no Linux box, nothing written into the repository.
Scratch: `scratchpad/review/asm/*.O{0,1,2}.s` (cpp11), `scratchpad/review/tms/o744*/`, `o822/`
(cl6x listings and objects), `tms/fold.py`, `tms/regs.py` (the two experiments below).

## 0. The shape of the gap, measured

| kernel | cpp11 -O2 CPU (vm6747) | cpp11 -O2 total (TI sim, handover) | stalls = total - CPU | cl6x 7.4.4 -O2 total | ratio total |
| --- | --- | --- | --- | --- | --- |
| fib | 4,355,822 | 15,387,798 | 11.0 M (72%) | 5,568,464 | 2.76 |
| hash | 22,314,120 | 92,297,743 | 70.0 M (76%) | 6,009,087 | 15.36 |
| isort | 10,127,100 | 32,785,549 | 22.7 M (69%) | 5,312,299 | 6.17 |
| matmul | 2,715,090 | 8,684,282 | 6.0 M (69%) | 1,294,024 | 6.71 |
| sieve | 12,700,543 | 39,160,159 | 26.5 M (68%) | 2,999,335 | 13.06 |
| virt | 3,580,521 | 14,912,534 | 11.3 M (76%) | 3,269,953 | 4.56 |
| total | 55,793,196 | 203,228,065 | **147.4 M (73%)** | 24,453,162 | 8.31 |

**Three quarters of cpp11's cycles on the real pipeline are L1D stalls, not instructions.** The
C64x+/C674x L1D is write-through with a small write buffer and no store-to-load forwarding: a load
that follows a store to the same line stalls until the write has drained to L2. The stack machine
does exactly that on every local (`STW A4, *A0` then `LDW *A4, A4` a few cycles later), on every
push/pop, on every parameter (stored to its slot in the prologue and reloaded at first use). So
**vm6747's count ranks changes by the wrong measure**: it sees 55.8 M where the chip sees 203 M,
and a change that removes a store/load pair is worth about three times what `-c` reports.
(hash's `% 26` is a second blind spot - see D1: the emulator charges a native call one cycle, and
hash makes 126,000 calls to `__c6xabi_remi`.)

Size at -O1, `.text` bytes of each kernel's object (`ofd6x`, cpp11 assembled by `cl6x -c`):

| kernel | cl6x -O2 -ms3 | cl6x -O1 | cl6x -O2 | cpp11 -O1 | cpp11 -O2 | cpp11 -O1 with fold (B1) |
| --- | --- | --- | --- | --- | --- | --- |
| fib | 128 | 128 | 128 | 352 | 352 | 320 |
| hash | 256 | 288 | 352 | 864 | 864 | 832 |
| isort | 352 | 384 | 480 | 1,344 | 1,344 | 1,216 |
| matmul | 416 | 704 | 1,152 | 1,952 | 1,952 | 1,664 |
| sieve | 352 | 320 | 416 | 1,088 | 1,088 | 928 |
| virt | 552 | 520 | 520 | 2,496 | 2,720 | 2,144 |
| total | **2,056** | 2,344 | 3,048 | **8,096** (3.94x) | 8,320 | 7,104 |

Plus 100-356 bytes per kernel of `.c6xabi.exidx`/`.extab` that cl6x's C build does not carry.

Two experiments drive the ranking in section B. Both are text rewrites of the `-O2` assembly run
on vm6747 (`tms/fold.py`, `tms/regs.py`), outputs checked against `.expected`:

| kernel | -O2 today | B1: frame fold mended | B2 on top: 4 locals of `sieve` in registers |
| --- | --- | --- | --- |
| fib | 4,355,822 | 3,954,632 (-9.2%) | |
| hash | 22,314,120 | 22,302,112 (-0.1%, its slots are past the 124-byte window) | |
| isort | 10,127,100 | 8,946,180 (-11.7%) | |
| matmul | 2,715,090 | 2,374,220 (-12.6%) | |
| sieve | 12,700,543 | 10,985,503 (-13.5%) | **8,826,615 (-30.5%)**, with 4 of its 5 memory ops per inner iteration gone |
| virt | 3,580,521 | 3,100,451 (-13.4%) | |

## A. Correctness and ABI

The third review's fourteen ABI items are closed in this tree (`b7ef373`, `6a9a1e1`, `ec53b18`,
`e5be0db`, `8f5e5ca`) and I found no new silent-wrong-answer in the ABI. What follows is what the
optimizer added or left latent.

**A1. The scheduler has no rule for the C67x two-cycle source read, and parallel packets will need
one.** `ADDDP`, `SUBDP`, `MPYDP`, `CMPxxDP`, `DPSP`, `DPINT`, `DPTRUNC`, `MPYSPDP` read the low words
of their sources in cycle 1 and the high words in cycle 2 (SPRUFE8, each instruction's "Pipeline"
table). `C6xSched.cpp` `Scheduler::feed` models only "reads at issue, writes land at issue+slots+1".
Today every instruction is its own packet, so the earliest a following write can land is issue+2
(after the second read) - the code is right by accident of being serial, and the emulator reads
both words at issue so it can never show the fault. The moment `||` packets are emitted (B6), an
instruction in the same packet writing A7 while `MPYDP A5:A4, A7:A6, ...` issues corrupts the
product silently on the chip and not on vm6747. Fix: in the packet builder, a DP-reading
instruction's source pair is "read until issue+1"; add the rule to vm6747's `Cpu.cpp` as well so
the emulator can refuse it. Test: a case with `x * y` in doubles where the next packet writes `y`'s
high word, run on TI's simulator (the emulator cannot see it).

**A2. `walkInPlace` and a landing pad: the reserved room is computed from the wrong list.**
`emitFunction` reserves `kInlineDepth * inliner_->largestFrame()` when `fn.hasLandingPads()`,
because a pad emits `spAdjust(-(kSaveBytes + frame_))` with `frame_` as it stands. `largestFrame()`
is the largest *eligible* callee frame in the unit (`Inliner::summarize`), which is the right upper
bound; but `walkInPlace` nests to depth 3 with `inlineTop_ = localBase_ + align8(fn.frameSize())`,
so the true bound is the sum of the three largest, and `kInlineDepth * largest` covers it. Fine. The
gap is the other way round: a function *without* landing pads whose body grows `frame_` after an
earlier `landingPad(...)` cannot exist, but a function with pads that inlines nothing wastes up to
`3 * largestFrame` bytes of stack per frame (virt: 3 pads, frame 96 -> larger). Not wrong, but a
stack cost per call of every function with a destructor-bearing local; recompute the reservation as
the sum of the frames of the sites `Inliner::allows` actually accepted in this function (they are
decided before the walk, in `summarize`).

**A3. A no-op copy and a cancelled pair survive `forwardMoves`.** `MV A4, A4` (matmul x2, isort,
virt) and `ADD B15, 8, B15` immediately followed by `SUB B15, 8, B15` (fib, virt: `spAdjust(area)`
then `push()`). Harmless, one cycle and one word each; `forwardMoves`' last loop should drop
`MV X, X`, and `visit(const Call&)` should fold the area close into the push that follows. Size
only, but it is what the comment-line policy calls "a claim the compiler makes": a peephole that
leaves `MV A4, A4` is not finished.

**A4. `foldPushPop` keeps a pushed value in A16-A31 across `__c6xabi_*` helper calls? No - but
document why.** The helpers are reached by `call()` = `MVKL/MVKH B3; B; NOP 5`, and `B` is a
`blockEnd`, so no push/pop pair spans one; and TI's helpers may clobber A16-A31 (they are
caller-saved). The invariant is "a `B` ends the block" and lives only in `blockEnd`; a future
`CALLP` (B5) must be added to `blockEnd` or the pairs will span calls. Write it beside `blockEnd`.

**A5. The emulator counts a native call as one cycle and models no L1D stall** (measurement, see
D1) - listed here because a compiler tuned against it will be tuned wrong.

**A6. Constant `MPY32` by 1.** `sieve`'s `comp[j] = 1` emits `MVK 1, A6; MPY32 A20, A6, A4; NOP 3`
for a 1-byte element (`ParserExpr`'s pointer scaling reaches the backend as `Mul` by the element
size). Right, and eleven cycles for nothing - see B3.

**A7. Over-acceptances already recorded** (`const struct point *p = a;` with `a` a `const void *`;
`<vector>::grow` leaking each old `string`) are in the handover; the second one costs Compiler++
cycles in `free` and is a real leak on the chip - fix `grow` to move-construct or copy-then-destroy.

## B. -O2 speed, ranked by expected gain per effort

Gains are on the six kernels' CPU cycles (vm6747) unless the TI total is stated; the stall column of
section 0 says which items pay two to three times more on the chip.

**B1. `foldFrame` never fires on a local access: its liveness test is wrong for the common shape.**
Evidence: in the six `-O2` listings `*-A15(` appears 4 times per kernel (the prologue's B3 save and
restore) against 29-90 `MVK k, A0; SUB A15, A0, R` frame addresses. `C6xSched.cpp` `foldFrame`
requires `deadAfter(v, i + 1, reg)` for the address register, and for `SUB A15, A0, A4; LDW *A4, A4`
the next line reads A4 (the *loaded* value) so it answers false - but the load itself consumed the
address and overwrote the register, so it is dead by construction. The store shape fails the same
way one line earlier: `MVK 44, A0; SUB A15, A0, A0; STW A4, *A0` asks `deadAfter(v, i, "A0")` from
the SUB, and the STW reads the SUB's A0. Both patterns are what every `visit(Var)` and
`visit(Assign)` emit. A third miss: the store's use is not adjacent after `forwardMoves` (`SUB
A15, A0, A6; MV A16, A4; STW A4, *A6`), and A6 is provably dead only with liveness across labels.
Proposed change (two parts): (a) in `foldFrame`, treat the address register as dead when the load's
destination (or a pair containing it) is that register, and skip the `deadAfter` on A0 when the SUB
writes A0; allow the use up to two lines after the SUB when the lines between touch neither `reg`
nor memory; (b) better, emit the folded form in the backend itself - `localAddr` knows the offset,
so `visit(Var)` on a local and `store` to a local can write `LDW *-A15(k), A4` / `STW A4, *-A15(k)`
directly when `k/size <= 31`, and `MVK k/size, A0; LDW *-A15[A0], A4` past that - which needs no
liveness at all and covers `LDDW/STDW` pairs. Measured (`tms/fold.py`, the (a) rules): -9% to -14%
CPU on five kernels, 0% on hash because its hot slots sit at 120-140 bytes (see B8). Size: -12%
at -O1 (7,104 of 8,096). Risk: low; the `*-A15(k)` form is already used and assembled. Test: emit
golden diff (every C6000 file should change), `tests/tms6747.sh` at -O1 and -O2, `CYCLES=1`.

**B2. Locals in registers - the item that pays on the chip.** Every local read is `LDW; NOP 4`
(5 cycles + a write-buffer stall), every write a `STW` that the next read stalls on; parameters are
stored in the prologue and reloaded. cl6x keeps `fib`'s `n` in A10 and B3 in B13 and touches memory
twice per call. Measured with `tms/regs.py` on the folded `sieve`: `n`, `i`, `count` in A10-A12 and
the 64-bit `j` in B11:B10 - 10,985,503 -> 8,826,615 CPU (-20% on top of B1, -30% on today) with the
`MV` copies still there for `forwardMoves` to take, and the inner loop's memory operations per
iteration go from 5 to 1 (the `STB` to `comp`), which is where the 26.5 M stall cycles of sieve
come from. Proposed design, as `A64Peep` round 3 did it on the Mac (text level, after B1):
a local is promotable when every reference to `A15` at its offset is a whole `LDW/STW *-A15(k)`
(or `LDDW/STDW` for a 64-bit one), no unfolded `SUB A15` with that offset survives (its address was
taken: `&x`, a struct, an argument slot copied by `copyBlock`, a landing pad's pointer/selector
slot), and the function has no `VaStart`. Registers: A10-A14, B10-B13 are callee-saved (B14 is DP,
never touched; A15/B15 the frame), and `savedRegs()`/`unwindWord()` already know how to save any of
A10-A15, B10-B15 in TI's pop order and say so in the exidx word, so the unwinder stays right; A10,
B10, A12, B12 are argument registers 7-10 and must be avoided in a function that passes more than
six arguments (`usesSavedArgRegs_`), A11/A13/B11/B13 likewise for a 64-bit one. For a leaf (no
`hasCall_`) A16-A31/B16-B31 are free and need no save (`foldPushPop` uses A16-A31: partition them).
Promote the most-referenced slots first, 64-bit ones to an even:odd pair. Each `LDW *-A15(k), D`
becomes `MV Rk, D` (the scheduler drops the NOP), each `STW S, *-A15(k)` becomes `MV S, Rk`, and
`forwardMoves` then removes most copies. Expected gain: -25% to -35% CPU on the loop kernels, and
on the TI total the larger share of the 147 M stall cycles - the honest estimate is the six kernels
from 8.3x to about 4x of cl6x. Risk: medium - liveness at labels (a promoted register is live
everywhere, so no liveness is needed for the promotion itself; what needs care is the argument
registers and the unwind mask). Test: `tests/tms6747.sh` -O1/-O2, a case with more than six
arguments and a promoted local, one with a 64-bit local, one with a landing pad; TI's simulator on
the kernels for the stall half.

**B3. Constant operands and address scaling: the multiplies that are not multiplies.** Three shapes,
all in the backend:
- `Mul` by a constant power of two (the parser's pointer scaling: `MPY32 A4, A6, A4; NOP 3` with
  A6 = 1, 4, 8, 192 in every array loop) -> nothing, `SHL n`, or better the scaled register form
  the ISA has for exactly this: `LDW *+A16[A4]` scales the index by the access size, so
  `sv[j]`, `mb[k][j]`, `comp[j]` are one instruction each; a row stride (192 = 24 doubles) is
  `SHL 3` plus an `ADDAD`/`ADDAW`. cl6x's isort loop is `LDW *A4, A4` and `STW A5, *+A6(4)`.
  Saves 4 cycles per array access (the `MPY32` and its 3 slots) - 2 to 4 per inner iteration in
  isort/matmul/sieve/hash.
- A `Binary` whose rhs is a `Num` in -16..15 (scst5) uses the immediate form: `CMPLT A4, 2, A0`,
  `ADD A4, 1, A4`, `SUB`, `SHL/SHR` by a constant, `AND/OR` with a small constant - today
  `MVK 2, A6; CMPLT A16, A6, A4`. One cycle and one word each; every loop test and step has one.
- Division and modulus by a constant: `hash` calls `__c6xabi_remi` 126,000 times for `% 26`
  (vm6747 -p) - cl6x emits the magic multiply (`0x4ec4ec4f`, `MPY32U` then shifts) and never
  calls; isort's `% 100000` and matmul's `% 7`, `% 5` the same. The x86 pipeline already has
  `divide-by-constant` on MIR; the C6000 wants it in the backend at `BinOp::Div/Mod` when the rhs
  folds: power of two -> `SHR`/`AND` (signed with the bias), otherwise `MPY32U` by the magic
  (4 cycles) + `SHRU`. TI's `remi` is about 40 cycles: 126,000 x 40 ~ 5 M of hash's 92 M total,
  invisible on the emulator (D1).
Effort: small each; test against clang's output for every constant shape and both signs.

**B4. Branch on the compare; drop the double negation.** `genTruth` always emits `isZero` +
`XOR 1` after the condition and `branchIfZero` adds `MV A4, A1`, so `if (i < n)` is
`CMPLT; CMPEQ 0; XOR 1; MV A4, A1; [!A1] B; NOP 5` - ten cycles where cl6x writes
`CMPLT .L1 A4,2,A0; [A0] B` (and fills the slots). Change: in `genTruth`, when the expression is a
comparison, `!`, `&&`/`||` or a `bool`-typed value it is already 0/1 - skip the two instructions;
make the compare write A1/A2 directly when its only consumer is the branch (a `branchOnCompare`
hook the Walker's `If`/`While`/`For` call instead of `genTruth` + `branchIfZero`, with the operator
inverted for the false edge: `CMPLT` -> `[!A1] B else`). Saves 3-4 cycles and 3 words per condition
(every loop iteration has one, hash's inner loop two). Effort: small. Risk: `Le`/`Ge` on floats are
two compares ORed (NaN) - keep them.

**B5. Fill the delay slots, and use the C64x+ branch forms.** Every `B` is followed by `NOP 5`
(6 cycles), every call is `MVKL ret, B3; MVKH ret, B3; B f; NOP 5` (8 cycles, 4 words), every
return `B L$return; NOP 5` then `LDW B3; MV; LDW A15; NOP 4; B B3; NOP 5` (17 cycles). cl6x:
`CALLP .S2 f, B3` (one word, six cycles, B3 set by the instruction itself), `BNOP label, n`,
`RETNOP B3, 5` with the restores in its slots, `ADDKPC`. vm6747 executes all four (`Isa.cpp:24-25`).
Three steps: (1) spell `call()` as `CALLP` (or `ADDKPC ret, B3, 4` after the `B` where a register
target needs `B B1`), returns as `RETNOP B3, 5`, jumps as `BNOP l, 5` - size and 2 cycles per call
for free; (2) in the scheduler, hoist a `B` over up to five preceding instructions that it does not
depend on and whose results land by the target's first cycle (a 0-slot instruction anywhere in the
slots, a load only in slot 1, `MPY32` in slots 1-2), so the loop's step and the epilogue's
restores run in the slots; (3) for a loop back-edge `B begin` whose target begins with the test,
rotate as the x86 `threadJumps` does. Expected: per call 8 -> 6 cycles, per taken branch up to 5
cycles back, per return ~10 cycles - fib (57,313 calls, 4 branches each) about -25%; loops -10%
to -20%. Effort: (1) half a day, (2) two or three days in `C6xSched`. Risk: `blockEnd` must know
the new mnemonics (A4); a hoisted load's landing cycle against the target (A1-style reasoning).

**B6. Parallel execute packets (`||`).** Not one in any cpp11 listing. After B2 and B5 the
sequential model in `C6xSched` (it already has reads/writes and latencies) can be list-scheduled
into packets: up to eight instructions, one per unit (L1 S1 D1 M1 L2 S2 D2 M2), each mnemonic's
unit set, at most two cross-path reads, at most one store and one load... per side per packet, the
two-cycle DP read (A1), and "a register written by a load may not be written by another instruction
landing in the same cycle". cl6x's inner loops are 1-6 cycles per iteration through `SPLOOP`;
packets without pipelining would put the kernels at perhaps 10-20 cycles per iteration against
today's 60-100. Effort: large (a week; unit assignment and the assembler's rules for `.D` constant
forms). Do after B2 - B5, when there are fewer instructions to pack.

**B7. The prologue and epilogue of a small function.** `fib` today: 6 instructions in, `STW A4`
of the parameter, 13 cycles out, and `ZERO A4` after the last `return`'s jump (dead code, one word
per function; `jump(returnLabel_)` for a `Return` that is the body's last statement should fall
through). A leaf with promoted locals (B2) needs no `A15` frame at all: cl6x's fib is 16 bytes of
save. Change: `needFrame` false when nothing reaches memory (after B2: no `localAddr` emitted, no
call, no sret); keep B3 in a callee-saved register instead of the stack when the function calls
(cl6x: `MV B3, B13`), which also removes the `LDW; NOP 4` before `B B3`; `ADDK -N, B15` for the
frame (one word, no `MVKL/MVKH 56, B0; SUB B15, B0, B15` - three words; `foldMvk` is not applied to
the prologue because it is emitted outside `c6xSchedule`). fib: ~20 of its 76 CPU cycles per call.

**B8. Slots the parser never reuses push hot locals past the addressing window.**
`Parser::allocateFrameSlot` only ever grows `frameSize_`: every call's `argSlot`/`resultSlot`, every
by-value copy, every guard flag, every temporary is a slot of its own, so `hash`'s four locals land
at 120-140 bytes - past `*-A15(k)`'s 124 (31 x 4) - and B1 buys hash nothing. Two fixes, either
side: in the parser, reuse temporaries' slots per full expression (as the x86 `locals` pass
recycles) and place named locals before temporaries; or in the backend, address temporaries from
B15 upward (`*+B15(k)`, the area is at the bottom of the frame) so the named locals stay near A15.
B2 makes it moot for promoted scalars, but struct temporaries (every `std::string` in Compiler++)
stay in memory and still want the short form.

**B9. `copyBlock` and the library's 28-byte strings.** A struct copy is, per word, `ADD from,off,A0;
LDW *A0,A3; NOP 4; ADD to,off,A0; STW A3,*A0` - nine cycles a word and a stall per pair, so a
`std::string` (heap_, len_, cap_, small_[16] = 28 bytes) returned or copied by value costs ~63
CPU cycles plus stalls, and Compiler++ does this on every `substr`, `+`, `to_string`. Change:
issue every load first (`LDDW *A4++, A7:A6`, `*A4++`, ... into A6-A9/B4-B9), then every store -
the C6000 pipelines loads back to back, so a 28-byte copy is 4 loads + 4 NOP... + 4 stores ~ 12
cycles; use `LDDW/STDW` when both sides are 8-aligned (a local slot and a parameter copy are).
Compiler++ measured 1.26x cl6x after the string buffer landed; its remaining profile is `free`
14% (the `vector<string>` leak in A7 and every `grow`), `strtoll` 10% (Compiler++'s own lexer,
TI's library on both sides) and RTTI 12% (`__dynamic_cast`, TI's, both sides) - so on Compiler++
the code generator's share is B9 and the calls, not the loops.

**B10. Inlining budgets on this target.** `kSmallMember = 12` nodes, depth 3, took Compiler++ from
1.32x to 1.26x for +10% text. After B2/B5/B7 a call is cheaper and a callee's body smaller, so the
budget can grow (x86 uses the level's site/caller/unit budgets through `Inliner::allows`; the C6000
adds its own `kSmallMember` gate on top - measure 12 -> 20 -> 30 once B2 lands). The decline list
(`CPP11_DECLINES`) already names the reasons; `inline-callee-noexcept` and
`inline-callee-struct-result` are the two worth lifting for Compiler++ (accessors returning a
`std::string` by value are exactly what B9 pays for).

## C. -O1 size

`-O1` and `-O2` differ only by inlining; the -O1 kernels are 3.94x cl6x `-ms3`. In order of bytes:

- **C1** B1's fold: -12% measured (2 words per local access). Free once B1 lands.
- **C2** B5(1): `CALLP` for `MVKL/MVKH/B/NOP` (3 words per call), `RETNOP B3, 5` for
  `B B3; NOP 5` (1 word), `BNOP` for `B; NOP` (1 word per branch). fib has 9 branches and 3 calls:
  ~15 words of 88.
- **C3** B7: `ADDK` for the frame (2 words per function), the dead `ZERO A4` (1), the `B L$return`
  before the epilogue when the return is last (2), B3 kept in a register (2). ~7 words per function.
- **C4** B4: three words per condition; B3's constant forms: one word per constant operand
  (`MVK` + the register form -> the immediate form); `MVK 1; MPY32` -> nothing (2 words + the NOP).
- **C5** B2: each promoted access is a `MV` (1 word) or nothing after forwarding, instead of
  `LDW/STW` (1) - size-neutral per access but it removes the parameter stores and the frame.
- **C6** A3's `MV A4, A4` and `ADD/SUB B15` pairs.
- **C7** The exception index: every function, `main` and leaves included, carries an 8-byte
  `.c6xabi.exidx` entry (fib: 100 bytes beside 352 of text; virt: 356). A C++ frame must be
  unwindable, so keep them - but a leaf that calls nothing and has no pad could use the compact
  `EXIDX_CANTUNWIND`-free inline word it already uses, which is 8 bytes either way; the saving is in
  `-O1` not emitting `__c6xabi_extab$f` rows for cleanup-free functions, which it already does. No
  change proposed; noted so the 3.94x is read against the 4x of text alone.
- **C8** A distinct -O1: today `Costs::forLevel(1)` disables only inlining. On this target -O1
  should also prefer `CALLP`/`BNOP`/`RETNOP` (they are both smaller and faster, so both levels
  want them) and decline B5(2)'s hoisting only where it duplicates code (it does not); the one
  real size/speed trade is `align`/unrolling, which the C6000 path does not do. So -O1 = -O2
  minus inlining remains right; the size gap is the same list as the speed gap.

Estimated -O1 after C1-C4, C6: about 4,300 of today's 8,096 bytes (2.1x cl6x -ms3); after C5 near
3,000 (1.5x). Matching `-ms3`'s 2,056 needs the packets (B6) - cl6x's `-ms3` still packs.

## D. Test and measurement gaps

**D1. vm6747 is a CPU-cycle oracle only, and charges a library call one cycle.** Two consequences:
(a) 73% of the chip's cycles (L1D stalls) are invisible, so a ranking made with `-c` favours
instruction-count changes over memory-traffic changes by about 3x; (b) `[native] __c6xabi_remi
126000 entries, 126000 cycles` in hash where TI's `remi` is tens of cycles each, likewise `divi`,
`memcpy`, `malloc`/`free`, `printf`. Proposed: give the emulator a per-native cost table measured
once on TI's simulator (a program calling each helper N times, `cycle.Total` differenced), and a
first-order L1D model - a load whose line was stored to within the last k cycles costs s stalls,
with (k, s) fitted so the six kernels' `-c` counts reproduce their TI totals (fib: 11.03 M stalls
over its ~57,000 x 8 store-then-load pairs gives ~24 cycles a pair as a starting point). Until
then, every claim in section B should be re-measured on TI's simulator before it is believed for
the chip, and a kernel's `-p` profile read beside its native-call counts.

**D2. No case pins the two-cycle DP read (A1)** and no test can, on the emulator. A TI-simulator
case set (tools/c6747/bench-par or c6747-three) with one double-precision program per DP
instruction whose next packet writes a source is the guard for B6.

**D3. The suite's cycle baseline exists (46,493,094 at -O2 over 337 cases, 1,724,928 bytes) but
nothing gates on it.** `tests/tms6747.sh CYCLES=1` should print the sum and the bytes and compare
against a recorded pair the way the emit golden is compared - a change that costs cycles is named
the way one that moves text is.

**D4. The kernels are six C programs; Compiler++ is one C++ workload.** Between them sits what the
C6000 will actually run: `tests/cases` has 337 programs but their cycle counts are dominated by
`printf` (D1). A third set - the twenty hottest functions of Compiler++ as standalone kernels
(`std::string::appendUnits`, `vector<...>::push_back`, `Lexer::next`, the RTTI-heavy `analyze*`)
with their inputs baked in - would give B9/B10 a measurement that the six kernels cannot.

**D5. `-O1` has no test of its own on this target** beyond the suite passing: nothing records that
`-O1` output is smaller than `-O2`'s (today it is larger by 0 to 224 bytes per kernel, virt's
inlining being the only difference). After C8, record both sizes.

**D6. The emit golden covers tms6747 at -O0 only** (`tests/out-emit.golden`, 693 C6000 files).
The scheduler and the inliner are exercised by the run suite, not by a text golden; record a
`-O2` golden for the C6000 (as x86 has) so B1-B7 can each say "N files changed, every changed line
is X".

## Recommended order of work

1. **B1** (foldFrame's liveness, then the backend emitting `*-A15(k)` itself) - one day, -10% CPU
   and -12% size measured, and it is what B2 stands on.
2. **B4 + B3** (branch on compare; immediate forms; scaled addressing; divide by constant) - two
   days, -10% to -15% CPU on the loop kernels, hash's 126,000 helper calls gone.
3. **B5(1) + B7** (CALLP/RETNOP/BNOP, ADDK, the dead tail, B3 in a register) - one day, size -25%
   and -25% on call-heavy code (fib, virt, Compiler++).
4. **B2** (locals in registers) - three to five days, the only item that reaches the 147 M stall
   cycles; -30% CPU on sieve measured, and on TI's total the kernels from 8.3x to ~4x.
5. **B8** (slot reuse or B15-relative temporaries) - one day, needed for Compiler++'s string-heavy
   frames where B2 does not apply.
6. **B9** (`copyBlock` as pipelined `LDDW/STDW`) - half a day, Compiler++'s remaining code-generator
   share.
7. **B5(2)** (delay-slot filling by hoisting) - three days.
8. **D1** (emulator cost model) before **B6** (packets), so the packet scheduler is tuned against a
   number that resembles the chip; then B6, a week, which is what cl6x -O2's 1-6 cycle loops are.
9. **B10** (budgets) last, re-measured on Compiler++ after 1-6.

Each step has its oracle: the emit golden at -O2 (D6) for "what moved", `tests/tms6747.sh` with
`CYCLES=1` for "how much", and TI's cycle-accurate simulator on the six kernels (`tools/c6747/
bench-par.cmd`) for the stall half that the emulator cannot see.


---

# ASM6x review, 2026-09-29 (read-only)

Subject: ASM6x at HEAD `ded44b4` (`build/asm6x.exe`, asm6x 0.2), assembling what cpp11 at
`f5420e5` (tms-opt) emits for `-arch tms6747`. Oracles: TI `cl6x -mv6740 --abi=eabi -c`
from CCS 7.4 (CGT 8.2.2) and CCS 5.5 (CGT 7.4.4) on the Windows box, `--no_compress
--symdebug:none` unless said otherwise, plus `ofd6x`/`dis6x` 8.2.2. Nothing in ASM6x or
C++Optimize was edited; the one prototype (C1) lives in a scratch copy of `src/`.

Everything measured is under `scratchpad/review/`: `h/` (the Compiler++ harness, 1,746
lines of C++ -> `h.O2.s` 401,657 lines / 8.5 MB, `h.O1.s` 368,028 lines), `small/`
(`tools/bench-c6x.cpp` at -O1/-O2), `corpus/` (the 347 `tests/out-emit/*.tms6747.s` of
C++Optimize), `box/` (TI's objects and logs, four variants each: `nc82` 8.2.2
no_compress, `c82` 8.2.2 compressed, `nc74`, `c74`), `big/` (the >64 KB extab probe and
TI's `tdeh_pr_c6000.cpp` grep), `probe/shapes.s` (BNOP/CALLP/MVK forms), `proto/` (the
hashed-lookup prototype), `wins/corpus/` (the corpus objects, diffs against both TI
versions, and the link), `sample.txt`/`sample2.txt` (profiles).

## Headline

**Accuracy.** Every object measured is table-identical to 8.2.2's: `tests/run.sh` green
(10/10, 442 probes 317 identical + 117 refused-by-both + 11 known, linkcheck 11/11, fresh
2/2); the 347-file corpus 347/347 identical and **347/347 linked by lnk6x**; the harness at
-O2 and -O1 (418 sections, 26,675 symbols, 1.41 MB of `.text`, 205 exception tables) and
bench-c6x at both levels - `c6xdiff` silent on all of them. `cmp` is *not* silent on any of
them: three cosmetic, fully characterised differences (A1-A3) stand between ASM6x and a
byte-identical object, and the test suite cannot see them because `c6xdiff` compares
tables by name. Against 7.4.4 the objects differ in two systematic, harmless ways (A4).
The "value truncated to 16 bits" warnings are TI's too (26 = 26 on the harness) and are
cpp11's to fix - cl6x switches personality for such a function (A5).

**Size.** The assembler-controlled lever is TI's compaction (16-bit instructions +
header-based fetch packets), which ASM6x does not do: 8.2.2's compressor shrinks cpp11's
own `.text` by **13.9 % (-O2) / 14.0 % (-O1)** on the harness, 11.4 % / 12.0 % on
bench-c6x, and **20.0 %** over the 347-file corpus (B1). Two more levers are the
compiler's, but the assembler already encodes them identically to TI (probe/shapes.s):
`B x; NOP 5` -> `BNOP x,5` (4.3 % of `.text`, same cycles) and the four-word call
sequence -> `CALLP` (7.6 %, two cycles fewer per call) (B2). NOP words are 15.1 % of
`.text` - that is the scheduler's prize, not the assembler's (B3).

**Assembler speed.** 1.9-2.0 s for the 8.5 MB harness, single-threaded, 51 MB RSS;
8.2.2 takes 2.8 s (9.8 s with compression), 7.4.4 3.3 s. 75 % of ASM6x's time is one
linear scan (`Unit::find`); a scratch prototype with an `unordered_map` assembles the same
file to a byte-identical object in **0.56 s** (C1).

---

## A. Accuracy

### A1. Local symbols are written in first-mention order; asm6x writes them in definition order, and two consecutive labels come out reversed

`Unit::ref()` creates the `Symbol` on the first *mention*, so a forward-referenced label
(`B L$end0` before `L$end0:`) precedes labels defined earlier, and `write_elf()` walks
`symbols` in that order. asm6x writes locals in **definition** order, and a label that
stood alone on its line is defined at the next emission from a **LIFO** stack, so
`L$..$end1:` / `L$..$step0:` on consecutive lines come out `step0, end1`. Measured: of
the harness's 24,477 named locals, all 1,528 adjacent equal-address pairs are in reverse
source order; a model "definition order, pending labels flushed LIFO at the next emission
or at `.sect`/`.text`/`.data`" reproduces asm6x's list **exactly** on bench-c6x (194
locals), h.O2 (24,477) and h.O1 (22,702).

Evidence (`box/h.O2.nc82.obj` vs `h/h.O2.obj`): `.symtab` entries equal as a set, order
differs from index 1315 on (`inline5,try7` vs `try7,inline5`; `end8,ret9,step8` vs
`ret9,step8,end8`); every `.rel*` section equal as a set *after* translating symbol
indices to names, byte-different before (first `.rel.text` entry `01a86000` vs
`01a76300`); `.strtab` follows symtab order in both. So this one ordering is the whole
of the symtab/strtab/rel byte difference.

Fix: give `Symbol` a definition ordinal set in `define()`/`constant()`; make
`Unit::pendingLabel` a vector flushed in reverse by `placeLabel()`; emit locals sorted by
the ordinal in `write_elf()` (the `.file` entry stays first, section symbols after the
named locals as now - asm6x puts them there: verified). Risk: nil for the link
(relocations are re-indexed by the same table); it also changes the *displayed* index in
`elfdump` output. Test: `cmp` against every recorded `.asm6x.obj` (see D2).

### A2. `sh_entsize` is 0 where asm6x writes 8 or 1

Four headers per object: `.c6xabi.exidx:*` (type 0x70000001) entsize **8**,
`.TI.symbol.alias` (0x7f000006) entsize **8**, `.strtab` and `.shstrtab` entsize **1**
(header 208/414/416/417 of `h.O2`; 8/14/16/17 of bench-c6x). `c6xdiff` does not compare
entsize. One line each in `src/elf.cpp`. Risk nil.

### A3. The ELF header says `e_phentsize = 0` and the first body sits at 52; asm6x writes 32 and 64

`h.O2`: mine `..3400 0000 0000 2800..`, TI `..3400 2000 0000 2800..` (`e_phentsize` 32,
`e_phnum` 0), and section 1's body at file offset 64 in TI's object against 52 in ours
(every later offset differs by the same 8-12 bytes, sizes identical). `src/elf.cpp:262`
and the first `pad4()` after the header (pad to 64). Risk nil; lnk6x is indifferent
(347 links). With A1-A3 done the objects should `cmp` equal - every other byte was shown
equal after the symbol re-indexing.

### A4. CGT 7.4.4 (CCS 5.5) writes a different object for the same source, in two ways

Against `nc74`: corpus 69 identical / 278 differ; harness 13 words + 4 relocations + 4
symbols. The two causes, both harmless to the link:

- **7.4.4 settles a branch to a `.weak` symbol defined in the same section in place**
  (`B _ZNSt12basic_stringIcE4initEPKcj` -> `027a1d12`, no relocation); 8.2.2 and ASM6x
  write `00000012` + `R_C6000_PCR_S21` (`src/unit.cpp:321`, `s.bind != B_WEAK`). 8.2.2's
  behaviour is the right one for a weak definition another unit may replace; keep it.
- **7.4.4 omits a local label nothing references** (789 of the 793 dropped labels over
  the corpus are mentioned exactly once in their source; the other 4 are named only
  inside a `$EXTAB_SCOPE(..)-$EXTAB_SCOPE(..)` difference, which folds to a constant):
  `L$..$fnend`, `L$..$step0`, `L$return$..`, `$chain`, `$noresult`, `$endcatch`. 8.2.2
  and ASM6x keep every label.

Not a defect; record it (D3), because CCS 5.5 is a named goal toolchain and today no test
asks 7.4.4 anything.

### A5. "value truncated to 16 bits": the tables are wrong for a function past 64 KB, and cl6x's answer is a different personality routine

26 warnings on `h.O2.s` (lines 201894-202097), 26 identical `[W0001]` warnings from
asm6x 8.2.2 - all `.half $EXTAB_SCOPE(L..) - $EXTAB_SCOPE(f) + 2` inside
`analyzeExprImpl`'s extab, whose scope offsets exceed 65,535. The truncated descriptors
are silently wrong at run time: an exception unwinding through that function would match
scopes at wrapped offsets. This is cpp11's table generator, not the assembler.

Measured with cl6x 8.2.2 (`big/`): a 9,000-call `try` function gets
`__c6xabi_unwind_cpp_pr2` ("long frame unwinding, **32-bit** scope",
`tdeh_pr_c6000.cpp:155`) with every descriptor as `.ulong`, the unwind word
`0x8201eb00, 0xa043e7e7` (personality index 2, one extra word of unwind byte-codes,
`POP compact B10 A11 A10` etc.); a small function gets `pr4` ("24-bit compact frame")
with `.half` descriptors (cpp11 writes `pr3`, "24-bit encoding, 16-bit scope" - also
16-bit). Fix (cpp11, `Tms6747.cpp`): when any scope offset or length of a function
exceeds 0xFFFF, emit `pr2` - `.ulong` descriptors and the frame's unwind instructions in
TI's byte-code form instead of the pr3 24-bit word - or refuse the function by name.
Either is better than a table both assemblers warn about and cpp11 ignores. ASM6x needs
nothing: it already assembles `.ulong` scope differences and PREL31 operators, and the
warning text/line agree with TI's.

### A6. Nothing else found

Probed beyond the corpus: `probe/shapes.s` (B/NOP, BNOP to a local, an external and a
register, MVKL/MVKH of a label into B3, CALLP to an external and a local, MVK/MVKL/MVKH
of 88, `MVK -32768`, RET/RETNOP) - identical to 8.2.2 (`dis6x` listing in the transcript).
The harness exercises, table-identically, the shapes the 2026-09-27/28 optimizer rounds
added since the last review: `*-A15[A0]` scaled register offsets (25), `*-R(n)` (2,179),
`.weak` inline members with in-section branches (13 sites 7.4.4 treats differently),
205 `.c6xabi.extab:*`/exidx pairs, `.bss` with alignment, `.neardata RW`. No `||`, `BNOP`,
`CALLP`, `ADDKPC` or 15-bit `*+B15(n)` (n > 124) appears in anything cpp11 emits today.

## B. Size and speed of the generated code

### B1. Compaction (C64x+ 16-bit instructions, header-based fetch packets) - the assembler's own lever

TI's default assembly compresses; ASM6x cannot, so the oracle runs `--no_compress`.
Measured on cpp11's own output (8.2.2 compressor, `.text` bytes):

| file | no_compress | compressed | saving |
|---|---|---|---|
| bench-c6x -O1 | 9,600 | 8,448 | 12.0 % |
| bench-c6x -O2 | 10,656 | 9,440 | 11.4 % |
| harness -O1 | 1,281,376 | 1,102,592 | 14.0 % |
| harness -O2 | 1,408,800 | 1,213,216 | 13.9 % |
| 347-file corpus (executable bytes) | 2,298,208 | 1,838,944 | **20.0 %** |

(7.4.4's compressor: harness -O2 1,213,216 -> 1,212,960, so the two TI versions do not
agree byte for byte with each other either - the achievable target is "as small as
8.2.2's" checked by size and by `dis6x`, not byte identity.) The CLAUDE.md figure
"8,448 through TI's assembler vs 9,600 through ASM6x" for bench-c6x -O1 is exactly this
gap. Cycles: same execute packets, so no direct change; the C6747's 32 KB L1P and 256 KB
L2 make a 14-20 % smaller `.text` worth program-cache misses on the 1.2 MB harness -
unmeasurable here (simulator reserved).

What it takes: per fetch packet, up to seven 16-bit forms plus a header word carrying the
layout, p-bits, and the register/side set; the 16-bit forms take A0-A7/B0-B7 (and a
"high" set via header bits), constants of 3-5 bits, and only some mnemonics; branch
targets inside compact code need `R_C6000_PCR_S7`/`S10` and TI keeps a compressed packet's
branch reach; scope/exidx offsets shift (label-based, so free); `.align`/`.nocmp`
regions and `R_C6000_NOCMP` data words are respected. A new pass between layout and
resolve, with its own settling loop (compaction changes addresses, which changes which
branches fit). Risk: high - a wrong compact word is a silent miscompile, so it must be
held to `dis6x` of 8.2.2's compressed objects for every case, and to `cmp` where
8.2.2's choices are reproduced. Size of the work: comparable to the whole encoder
rewritten for the 09-19 review. Test: record `*.asm6x.c.obj` (compressed) beside the
`nc` objects; compare instruction *sequences* by `dis6x` and `.text` size; run the
corpus on the emulator and the cycle-accurate simulator.

### B2. Two folds the assembler encodes today and cpp11 does not write: BNOP and CALLP

Census of `h.O2.s`: 15,179 `B` and every one is followed by `NOP 5` (1,766 by register);
8,925 calls are `MVKL ret,B3; MVKH ret,B3; B f; NOP 5` (four words). cl6x -O2's own code
for `sieve.c`/`hash.c` writes BNOP (4 + 1) and CALLP (4) and no plain `B` at all.

- `B x; NOP 5` -> `BNOP x, 5`: one word for two, identical timing (the count fills the
  same five slots), 60,716 bytes = **4.3 %** of `.text`. Reach: BNOP's displacement is
  PCR_S12 (±2,048 words = ±8 KB from the fetch packet); a label beyond that keeps `B`.
  The register form `BNOP B3, 5` is C64x+ and assembles (`008ca362`).
- the call -> `CALLP f, B3`: one word for four, 107,100 bytes = **7.6 %**, and two cycles
  fewer per call (the MVKL/MVKH pair). CALLP takes a PCR_S21 label and A3/B3 only, so
  indirect calls (`B B1`) stay as they are.

Both are the compiler's to emit (it owns the schedule and the return label); ASM6x
already assembles both identically to 8.2.2 (`probe/shapes.s`, review probes 096-099).
An assembler-side `--fold` pass is possible (a `B` immediately followed by `NOP 5` with
no label between them and no `||`) but would break the "same source, same object"
oracle, so it should stay opt-in if written at all. Expected combined gain ~12 % of
`.text` at -O2 with no semantic change; the MVKL/MVKH-of-a-small-constant pairs (1,423 of
1,438 numeric pairs, e.g. `MVKL 88,B0; MVKH 88,B0` -> `MVK 88,B0`) are 0.4 % more and one
cycle each.

### B3. NOPs are 15 % of the text and 100 % of themselves in cycles

53,253 NOP words (20,234 `NOP 4` after loads, 19,940 `NOP 5` after branches, 11,249
`NOP 3`) = 213 KB of a 1.41 MB `.text`, and every one is a cycle. Filling delay slots
and packetising (`||`) is a scheduler, i.e. cpp11's next optimizer round; the assembler
is ready for it - unit assignment and packet retargeting were mended in the 09-19 review
and the packets in `tests/probes` and edge 154-166 are held to asm6x.

### B4. Padding and alignment: nothing to gain

`.text` is padded to 32 once per section (asm6x does the same); the harness has one
`.text` and no `.align` in code. Data sections align as TI's do.

## C. Assembler performance

### C1. `Unit::find` is a linear scan over the symbol vector - 75 % of the run

`sample` on the harness: 546 of ~740 in-process samples in `Unit::ref` (which calls
`find`, `src/unit.cpp:111`), against 18 in `table_form` and 11 in `split_line`. 26,675
symbols x ~60,000 symbol mentions x 3 passes. Prototype (`proto/src`, an
`std::unordered_map<std::string,int>` filled in `ref()` and read in `find()`, 6 lines):

| | harness -O2, 8.5 MB | object |
|---|---|---|
| `build/asm6x.exe` | 1.97-2.03 s, 51.5 MB RSS | |
| prototype | **0.56 s**, 51.1 MB | byte-identical (`cmp`) |
| cl6x 8.2.2 `--no_compress` | 2.8 s | |
| cl6x 8.2.2 compressed | 9.8 s | |
| cl6x 7.4.4 `--no_compress` | 3.3 s | |

Risk nil (the map mirrors the vector; `symbols` is only ever appended). After it the
profile is `strcmp` in `table_form` (79 samples: the 356-row form table scanned
linearly per instruction, three times over), `split_line` (24: every line re-lexed on
each of the three passes), `upper()`/malloc. An index of `kForms` by mnemonic and caching
each line's tokens across passes would take another ~40 %; not needed for the goals.

### C2. Passes

Three passes always (pass 1 places nothing forward, pass 2 places, pass 3 confirms), which
is inherent to the settle-until-nothing-moves design and fine at these sizes. One thread
per input file; the one-file harness gets one thread, and the compilerpp driver
assembles one file - nothing to gain from more threads there.

## D. Test gaps

### D1. `c6xdiff` compares tables by name and never bytes, so A1-A3 were invisible

It does not compare `sh_entsize`, `e_phentsize`, file offsets, symbol order or relocation
order. Add a final `cmp` (or a `--bytes` mode) once A1-A3 land, and keep it: a
byte-identical object is the review's stated goal and is one command to check.

### D2. No large input in the tests

`tests/enc` is 10 small files; the 442 probes are one question each. The harness (400K
lines, 418 sections, 26,675 symbols, 205 exception tables, the scaled register offsets,
weak members, 15 branch-to-weak sites) is the only input of its kind and assembled
table-identically today - record its `.asm6x.obj` (3.6 MB; or a `--rounds 1` reduced
harness) and time it, so speed (C1) and the big-function warnings (A5) are measured on
every run. `tests/windows.sh` should also compare warning lines with `*.asm6x.log`
(recorded but unread).

### D3. CCS 5.5 / CGT 7.4.4 is a goal toolchain and no test asks it anything

Record `*.nc74.obj` beside `*.asm6x.obj` for `tests/enc` and the corpus, with the two
known differences (A4) filtered by `c6xdiff` (a `--ti74` mode: accept an in-place weak
branch and a missing unreferenced local). Today the only 7.4.4 check is that lnk6x 7.4.4
links (it does: `tools/c6747-compilerpp`'s `windows-cpp11` leg).

### D4. The corpus should be the *current* one, and it is not wired to the compiler's tree

`tests/windows.sh <dir>` needs a directory of `.s`; the 347 `tests/out-emit/*.tms6747.s`
of C++Optimize are that corpus and were last run through it on 2026-09-19, before the
optimizer rounds. A one-line wrapper naming that directory (or `tools/verify-three`'s
c6747 leg calling it) would keep the two in step. Two script faults met on the way:
`xargs -I{}` fails with "command line cannot be assembled, too long" when the corpus
path is long (macOS caps the `-I` replacement at 255 bytes - use a `for` loop or `-n 1`),
and the box's fixed `C:\asm6x-tests` root cannot be redirected.

### D5. Compressed objects are not recorded

If B1 is attempted, `tests/windows/record.cmd` needs a second run without
`--no_compress` and `c6xdiff` a `dis6x`-level comparison mode; until then the size
figures above are the only record.

## Recommended order

1. **A1 + A2 + A3, then a `cmp` in `tests/run.sh`/`windows.sh` (D1)** - three small
   changes in `src/elf.cpp`/`src/unit.cpp` that make every object byte-identical to
   8.2.2's, and the test that keeps it so. Half a day; the ordering model is exact on
   three files of 194-24,477 locals.
2. **C1, the hashed symbol index** - six lines, 3.5x on the harness, byte-identical
   output proven by the prototype.
3. **D2 + D3 + D4** - the harness and the current corpus as recorded inputs, 7.4.4's
   objects beside 8.2.2's with the two differences filtered, the corpus wired to
   C++Optimize's `tests/out-emit`. Pays for itself the first time cpp11's backend changes.
4. **A5, in cpp11** - `pr2` with `.ulong` descriptors (or a named refusal) for a function
   whose scope offsets pass 0xFFFF; today the harness ships a silently wrong unwind table
   for `analyzeExprImpl` and both assemblers warn about it 26 times.
5. **B2, in cpp11** - `BNOP` for `B; NOP 5` within ±8 KB and `CALLP` for the call
   sequence: ~12 % of `.text` at -O2, two cycles per call, no assembler work, and cl6x
   -O2 does exactly this.
6. **B1, compaction** - the one large assembler project, worth 14 % on the harness and
   20 % on the corpus and the only way to reach cl6x -ms3's sizes; do it after 1-3 so
   that it is held to recorded compressed objects and `dis6x` from the first commit.
   B3 (delay-slot filling and packets) is the cycles project and belongs to the
   compiler's scheduler; the assembler's packet handling is ready for it.


---

# LNK6x review - against lnk6x 7.4.4 and 8.2.2 on real programs (Fable 5.1, 2026-09-29)

Reviewed read-only: `~/Developer/Claude/LNK6x` at `a6d9462` (src/ 2,376 lines), its bed, and
`docs/known.md`. Nothing in LNK6x, ASM6x or C++Optimize was edited; every experiment ran in the
scratchpad (`.../scratchpad/review/`, kept) and on the Windows box under `C:\cxx1\review-lnk\`.
Neither TI simulator nor the Linux box was touched.

**Oracles.** TI's `lnk6x` from CGT 7.4.4 (CCS 5.5 - the one both simulator legs of
`tools/c6747-three` and `run-windows.cmd` link with) against the 7.4.4 `rts6740_elf_eh.lib` in
`C:\cxx1\c6747-lib`; and `lnk6x` 8.2.2 against its own mklib runtime (`VM6747\tilib`, the copy
checked into `tests/ref`). 8.2.2 refuses the 7.4.4 library outright ("C++ or C object files
built with compiler versions before 8.0.0 are incompatible"), so every comparison below is
version-consistent, and most are against 7.4.4, which is the toolchain the user's speed numbers
come from.

**Inputs.** The ten programs of `tools/c6747/bench` and `programs/` compiled by `cpp11 -arch
tms6747 -O2` (and `-O1`) and assembled by ASM6x; the ten-file Compiler++ harness from
`make-harness.py --workload` (`h.O2.obj`: 3.6 MB, 418 sections, 26,675 symbols, 31,889
relocations); four kernels compiled by `cl6x 7.4.4 -O2` itself; and a purpose-built RLE probe.
Command files: TI's own `C6747.cmd` (SHRAM) and `C6747-ddr.cmd`, `--rom_model`, the exact
options `sim-ccs55.cmd` passes. Comparison: `tests/elfdiff.py`, a map-by-map contribution
differ, a decoder for TI's rle24 stream written from `dis6x` of `copy_decompress_rle.obj`, and
`ofd6x`. Timing: wall clock, 3-5 runs.

**Verdict in three lines.** LNK6x links every input, including cl6x's own objects and the
3.6 MB harness (once EHTYPE is applied), and its loadable image is within 0x40 bytes of
lnk6x's on every kernel. But **it has never written a single byte of any unwind index** - every
`.c6xabi.exidx` it emits is zero - so no C++ exception can be caught in any program it links,
and its RLE encoder corrupts `.cinit` for any initialised section that uses all 256 byte values.
Those two, plus a 130x link-time fix that is one function, are the work.

---

## Summary table

| # | finding | class | evidence | gain |
|---|---|---|---|---|
| A1 | `.c6xabi.exidx` is written as zeros in every image | accuracy, **runtime** | fib: 0 non-zero bytes of 0x170; harness: 0 of 0x2E08; TI: 361 of 0x1C8 | C++ EH works at all |
| A2 | rle24 stream corrupt when no byte value is free, and long runs not in lnk6x's form | accuracy, **runtime**, data-dependent | probe: 72,811 bytes decoded for a 71,376-byte section; TI decodes exactly | correct `.cinit`; byte-identity |
| A3 | `R_C6000_EHTYPE` refused | accuracy, **blocks Compiler++** | `.c6xabi.extab:_Znaj: relocation R_C6000_EHTYPE (28) is not handled` | harness links (it is `S + A`) |
| A4 | `.bss` allocated first regardless of size | accuracy, layout | sieve: TI `.text` 80000000 `.bss` 800092E0 (both versions); ours `.bss` first, `.text` 80004E40 | matches lnk6x |
| A5 | PCR_S21 (and every narrow field) silently truncated - no trampolines, no range error | accuracy, silent | q15 links rc=0 with a call 16 MB away, 906 bytes differ | refuse, then trampolines |
| A6 | `.TI.symbol.alias` ignored: `remove.obj` pulled where lnk6x resolves `remove -> unlink` | accuracy + 32 B size | every kernel `.text` +0x20 = `remove.obj (.text:remove)`; 13 runtime members carry aliases | identical `.text` |
| A7 | `.stack` not ALLOC, align 1; `.stack`/`.sysmem` tie broken the wrong way | accuracy | `.stack flags 0/3 align 1/8`; ours `.sysmem` before `.stack` | matches |
| A8 | option precedence: the line always wins over the file; lnk6x takes the last one read | accuracy | box: `--heap_size=0x800 C6747-ddr.cmd` -> 16 MB heap; reversed -> 0x800 | matches |
| A9 | `.cinit` record order, handler-table order, `.cinit` alignment differ from 7.4.4 | accuracy, byte-identity | harness: TI `.fardata .data .neardata .far`, handlers `rle24 none zero_init`; ours `.data .fardata ...`, `zero_init rle24 none`; align 8 vs 4 | byte-identity |
| A10 | no `EXIDX_CANTUNWIND` synthesis/merge; index not sorted by address | accuracy (throw through code with no entry) | TI 58 entries (24 cantunwind) vs 46 for fib; scratch copy: not sorted | correct unwinding, identity |
| A11 | vague-linkage duplicate: which copy wins differs | fidelity | virt: ours keeps `typeinfo_.obj`'s `_ZTIv`, TI keeps `tdeh_cpp_abi.obj`'s | identity |
| B1 | dead stub kept (A6), missing cantunwind entries (A10) | size | net loadable image is 0x38-0x40 B *smaller* than TI's | - |
| B2 | non-ALLOC sections dropped: no DWARF in the image | size of file (4x smaller), debuggability | ours 85 KB vs TI 362 KB; 7 `.debug_*` sections, 277 KB | keep debug on request |
| B3 | symbol table thinner than TI's | fidelity | 1,290 vs 2,698 entries; missing per-input-section symbols | - |
| C1 | `Strtab::add` is a linear search over the string table | **link speed** | harness 2.85 s -> 0.022 s with a hash (identical image); TI 0.22 s | 130x |
| C2 | archive pull and `pull_symbol` are linear scans of a 4,415-entry index | link speed | invisible at 22 ms today | - |
| D1 | the bed has no case whose LNK6x image contains an unwind index or a C++ throw | test gap | A1 survived 18 green probes and the 2026-09-28 hello run | - |
| D2 | nothing runs an LNK6x image on TI's simulator; the corpus is stale (2026-09-20) | test gap | `tests/corpus/*/ref/*ours*.log` dated 09-20; 4 bed probes DIFF at HEAD | - |
| D3 | no probe for: all-256-value data, runs > 255 / > 65535, aliases, cl6x objects, option order, two-TU C++ | test gap | this review wrote the first four ad hoc | - |

---

## The measurements the findings rest on

### Kernels, cpp11 -O2, C6747.cmd, lnk6x 7.4.4 (all ten look alike; fib shown)

```
                     LNK6x                 lnk6x 7.4.4
file size            81,704                355,812   (7 .debug_* sections, 277 KB, only in TI's)
loadable bytes       0x95CA                0x960A    (ours 0x40 smaller: .text +0x20, .exidx -0x58)
.text                0x8FA0 @80000000      0x8F80 @80000000   +remove.obj(.text:remove) 0x20
.stack               0x800 flags 0 align 1 0x800 flags 3 align 8
.sysmem/.stack order .sysmem first          .stack first (tie 0x800/0x800 -> name)
.cinit               0x98 align 8           0x98 align 4; streams differ by the long-form run
.c6xabi.exidx        0x170, 46 entries, ALL ZERO   0x1C8, 58 entries, sorted, 24 cantunwind
.c6xabi.attributes   57 B (8.2.2's constant) 58 B
.TI.section.flags    26 B                   28 B
symbols              1,290                  2,698
```
`sieve`, whose `.bss` (0x4E21) is smaller than `.text` (0x92E0), also differs in every address:
TI (7.4.4 *and* 8.2.2) puts `.text` at 0x80000000 and `.bss` at 0x800092E0/0x8000A520; LNK6x
puts `.bss` first and `.text` at 0x80004E40 (A4).

### cl6x 7.4.4 -O2 objects (sieve, hash, structs, virt)

Relocation types in those objects: PCR_S21, ABS_L16, ABS_H16 (and ABS32 in virt) - no SBR
forms, so TI's default data model needs nothing LNK6x lacks. All four link; the differences are
exactly the kernels' (A4-A7). The runtime library itself uses types 0,1,4,9,10,25 and three
sites each of 28, 29, 30.

### The Compiler++ harness (h.O2.obj + C6747-ddr.cmd)

LNK6x at HEAD: refused (A3). Scratch copy with EHTYPE = `S+A`: links, 3.36 MB, in 2.85 s
(TI: 0.22 s); with a hashed string table, **0.022 s** and a byte-identical image (C1).
Against TI's image: `.text` 0x169360 vs 0x169340 (A6), `.sysmem` 0x800 vs 0x1000000 (A8, so
every address shifts), `.cinit` 0x498 vs 0x494 (A2/A9), `.c6xabi.exidx` 0x2E08 vs 0x2E68 (A10),
1,473 entries all zero (A1). Compiler++ has 4 `throw` and 2 `catch` sites.

---

## A. Accuracy defects

### A1. The unwind index is never written - critical

`elf.cpp:50` loads a section's bytes only when `c.type == SHT_PROGBITS`. `.c6xabi.exidx` is
`SHT_C6000_UNWIND` (0x70000001) in every object - cl6x 7.4.4's, 8.2.2's and ASM6x's alike - so
its `data` stays empty; `fix_up` skips a contribution with empty data (`link.cpp:872`), so its
PREL31 relocations are never applied; `write_image` skips it too (`image.cpp:296`), so the
output section - PROGBITS, correctly sized, in a PT_LOAD - is zero-filled.

Evidence: `ours/fib.O2.out` `.c6xabi.exidx` 0x170 bytes, **0 non-zero**; `ours/harness.out`
0x2E08 bytes, 0 non-zero; TI's fib index has 361 non-zero bytes, first words
`7fffab1c 00000001 7fffb738 84073bf7 ...`. The `.c6xabi.extab` beside it *is* written (59 of 92
bytes non-zero, same as TI), which is why nothing noticed: the personality routine and the
tables all link, and a program that never throws runs. `__TI_unwind`'s binary search over a
zero table finds "function = the entry's own address, EHT pointer 0" for every PC: the first
`throw` in any LNK6x-linked program ends in `terminate` or worse. This is why known.md's
"42 entries against 37" was ever the comparison - the bytes were never looked at.

**Change.** Load bytes for every section that is not `SHT_NOBITS`/`SHT_NULL` and not a
symbol/string/relocation table (the scratch copy did this in one line and the PREL31 words
then came out; 46 of 46 entries name the right functions). Then A10.

**Test.** A bed probe with a C++ `throw`/`catch` whose image is compared *and run*; and assert
in `write_image` that no output PROGBITS section with parts is all zero.

### A2. rle24: corrupt stream when no byte value is free; long runs not lnk6x's

`escape_for` returns 0 when every byte value occurs; `rle24_encode` then writes a literal that
equals the escape as `E 1 E`. TI's decoder (read off `dis6x copy_decompress_rle.obj`:
`[B0] CMPGTU 4,B0,B1 / [B1] MV B7,A8 / [!B1] LDBU value`) treats a count of 1..3 as "that many
copies of the escape byte itself, **no value byte follows**" - so the third byte is read as the
next token, and the stream is misparsed from there.

Evidence: the probe (`rle/rle.s`: bytes 0..255, isolated zeros, a 300-run, a 70,000-run;
linked with hello-style `main`). LNK6x's `.fardata` stream, decoded by the model of the
decompressor: **72,811 bytes for a 71,376-byte section, CORRUPT**. TI 7.4.4's: 396 bytes,
decodes to exactly 71,376, identical to the source. The overrun lands in whatever follows
`.fardata` at run time. Any real program whose `.fardata`/`.data` holds a 256-entry table or
binary blob hits this.

Three rules read off TI's stream, none of which LNK6x has:
- **Escape = the least frequent byte value, smallest on a tie** (0x0A here: every value in
  10..255 occurs once, 0x0A is the smallest). "Smallest absent" is the special case.
- **Runs of 256..65535: `E 00 hi lo v`** (16-bit big-endian): `0a 00 01 2c 07` for 300 x 7.
  LNK6x writes runs of 255 back to back (`00 ff 00` x 274 for the 70,000 zeros) - decodable
  but 4x the bytes and never byte-identical; fib's 411-zero run is the one-byte difference
  in every kernel's `.cinit`.
- **Runs of 65536..2^24-1: `E 00 00 hi mid lo v`** (the "24" in rle24: when the 16-bit length
  is < 256 two more bytes are read): `0a 00 00 01 11 70 00` for 70,000 x 0.
- Terminator `E 00 00 00` (4 bytes) - LNK6x has this right. Threshold for a run is 4 (TI
  writes `04 04 00` for four zeros and three zeros as literals) - right too.

**Change.** Frequency-based escape; emit `E n E` only for n in 1..3 and never a value byte;
the two long forms. **Test.** Re-link the probe and decode with the model (`rledec.py`); add
it to the bed with TI's image (`back2/rle55.out` is in the scratchpad).

### A3. `R_C6000_EHTYPE` is `S + A` (an absolute word) - blocks Compiler++

Measured on TI's harness image: `.c6xabi.extab:_Znaj` at 0xC11BE884, word at +0xC =
`c11bb418` = `_ZTISt9bad_alloc`'s address exactly (REL, addend 0 in place). Three sites in the
7.4.4 runtime (`array_new`, `new_`, `newnothrow`), all `catch (bad_alloc)` type references,
pulled by anything using `new[]` or the nothrow forms. `elf.cpp:102` already reads the REL
addend for EHTYPE; `reloc.cpp` needs `case R_C6000_EHTYPE: wr32(p, V)`. The scratch copy
proved it: the harness links and its extab words match TI's up to the address shift.

### A4. `.bss` is not allocated first - output sections go in descending size, `.bss` included

q03 (`.bss` 0x100 > `.text` 0x20) and q12 (`.bss` 0x40 > `.text` 0x20) never distinguished
"`.bss` first" from "biggest first". sieve does: `.bss` 0x4E21 < `.text` 0x92E0, and both
lnk6x versions lay `.text` at 0x80000000 then `.bss`, `.stack`, `.sysmem`, `.const`,
`.fardata` ... in size order. `link.cpp:730-731` should drop the special case;
`__TI_STATIC_BASE` still points at `.bss` wherever it lands.

### A5. Out-of-range relocations are truncated silently

`field()` masks; nothing checks that `(V - P) >> 2` fits 21 bits, or that ABS16/ABS8/
ABS_S16/PCR_S12/S10/S7 fit. q15 (a call 16 MB away) links with rc=0 and an empty log; lnk6x
writes a 32-byte `$Tramp$S$$name`. With `C6747.cmd` everything is in 128 KB and with
`C6747-ddr.cmd` the harness's `.text` is 1.4 MB, so today nothing is out of the +-4 MB reach -
but the failure is a silent wrong branch. **Change**: refuse with the symbol named; trampolines
after (q15 has the shape). **Test**: q15 becomes an `.error` case until trampolines land.

### A6. `.TI.symbol.alias` is ignored, so `remove.obj` is pulled and kept

`remove.obj`'s `.text:remove` is 32 bytes of `B unlink` + NOPs, and its `.TI.symbol.alias`
(17 bytes: `01 00 00 00 | 01 00 | "TI\0" | 26 00 00 00 25 00 00 00` = version, count, magic,
then {alias symbol index, target symbol index} pairs) says `remove -> unlink`. lnk6x gives
`remove` unlink's address (map: `80008020 remove` and `80008020 unlink`) and drops the stub;
LNK6x keeps it (`+0x20 .text` in all ten kernels and the harness, and every part after it
shifts). 13 runtime members carry alias tables (`clock -> HOSTclock`, `__c6xabi_divremi ->
div`, the C1/C2 and D1/D2 pairs of the iostream classes...). **Change**: read the table in
`elf.cpp`; in `take_module` define the alias as the target's (module, symbol); the stub section
is then unreferenced and elimination drops it. Also the archive pull should resolve one wanted
name at a time against `defined`, as lnk6x's index walk does, rather than pulling a whole pass
at once - that is the second reason `remove.obj` came in.

### A7. `.stack`: flags, alignment, and its place against `.sysmem`

TI: `.stack` `SHF_ALLOC|SHF_WRITE`, align 8 (both versions, once sized); ours: flags 0, align 1,
outside every PT_LOAD (`image.cpp:66` lays out only ALLOC sections - the NOBITS `.stack` gets
`sh_offset` 0). And the size used for the descending-size sort is `reserve + parts` for
`.sysmem` (0x808) but `reserve` for `.stack` (0x800), so `.sysmem` wins a tie lnk6x gives to
`.stack` by name (fib: TI `.stack` 80008F80 `.sysmem` 80009780; ours the reverse). The
reservation is a floor - use `max(reserve, parts)` for the sort key as `allocate` already does
for the size.

### A8. Option precedence is positional in lnk6x

Box test (hello, 7.4.4): `--heap_size=0x800 ... C6747-ddr.cmd` (whose `-heap 0x01000000`
comes later) -> `.sysmem` 0x1000000; `C6747-ddr.cmd --heap_size=0x800` -> 0x800.
`main.cpp:18-24` lets the line win always. The harness link in `run-windows.cmd` passes no
size, so today only my test line differs - but `sim-ccs55.cmd`'s line does pass sizes before
the file, and the day someone adds `-heap` to `C6747.cmd` the images part. **Change**: apply
options in the order they are met, the file's contents at its position.

### A9. `.cinit` composition against 7.4.4

- **Record order**: TI `.fardata`(0x6F0) `.data`(0x74) `.neardata`(0x1C) then the zero-init
  `.far`; ours output-section order (`.data .fardata .neardata .far`). Descending run size,
  or ascending run address - both fit; a probe with a small section at a low address decides.
- **Handler table**: 7.4.4 fib/virt: `__TI_zero_init, rle24, none`; 7.4.4 harness: `rle24,
  none, __TI_zero_init`; ours always zero_init first. Not "first needed", not by address; the
  module order in the MODULE SUMMARY is the next hypothesis.
- **`.cinit` alignment**: 4 in 7.4.4, 8 in 8.2.2 (ours 8). And 8.2.2 never uses
  `__TI_zero_init` at all - its `.far` record is rle (11 bytes, escape 01) - so the zero-fill
  rule known.md read off hello is 7.4.4's. The two versions cannot both be matched; pick 7.4.4
  (the simulator toolchain) and say so in known.md, or key it on a `--cgt=` switch.

### A10. Index sorting, `EXIDX_CANTUNWIND` synthesis and merging

With bytes loaded (scratch), ours has 46 entries in `BiggerPart` order - **not sorted by
function address**, which the unwinder's binary search requires - and no entries for kept
functions without one; TI has 58 (24 cantunwind), sorted, and merges the object-level
cantunwind entries ours keeps as 12-14 separate ones (`__c6xabi_unwind_cpp_pr1/2`,
`_ZSt9terminatev`, `__abort_execution`...). Order: sort by target address, then synthesise one
`{PREL31 fn, 1}` per live `.text` part with no entry, then merge runs of consecutive
cantunwind entries into the first. `__TI_UNWIND_TABLE_START/END` already bracket it.

### A11. Which duplicate of a group section survives

Both keep exactly one `_ZTSv`, `_ZTIv`, `_ZTISt9exception`, `_ZTVSt9exception`; ours
`typeinfo_.obj`'s, TI `tdeh_cpp_abi.obj`'s - the first *in TI's module order*, which is the
archive-index walk order, not ours. Cosmetic until A6's pull order is fixed, then likely free.

### Smaller accuracy notes

- `-z` on the line is "unknown option" (exit 2): LNK6x cannot be dropped onto cl6x's own
  link line as copied from `sim-ccs55.cmd`. Accept and ignore `-z`, `-a`, `--xml_link_info=`,
  `--cinit_compression=`.
- Empty-section flags differ by version: 7.4.4 writes 1 for empty `.bss/.data/.neardata/
  .init_array` and 0 for `.rodata`; 8.2.2 (the bed's table) 3 and 2. Harmless; note it.
- `.c6xabi.attributes` (57 B, 8.2.2's bare value) and `.TI.section.flags` (26 B) are still
  constants; 7.4.4 writes 58 and 28 bytes, 8.2.2 with a library 61 and 28. The tags differ
  (`08 08 0a 05 0c 05` vs `08 09 0a 03 0c 03`: ISA/ABI tags merged from the library). Wrong for
  every real link; cosmetic to the loader.
- `SHN_C6000_SCOMMON`, weak, PCR_L16/H16, PREL31: checked against these images, all right.

## B. Size and speed of the image

- **Loadable bytes**: LNK6x's image is 0x38-0x40 bytes *smaller* than lnk6x's on every kernel
  - +0x20 dead `remove` stub (A6), -0x58 missing cantunwind entries (A10). After both, equal.
  Elimination otherwise matches part for part (164 of 164 `.text` parts, 36/36 `.const`,
  8/8 `.fardata`, same holes filled first-fit). Nothing in the image is slower than lnk6x's:
  the code is the same bytes at offsets that differ by 0x20.
- **`.cinit`**: same size after alignment today (fib 0x98/0x98) because padding absorbs the
  long-form byte; the 70,000-zero run costs 822 bytes against TI's 7. Real programs with large
  zero-initialised `.fardata` (a `static` table) pay this.
- **File size**: ours 81 KB vs 355 KB - LNK6x drops every non-ALLOC section, so the DWARF the
  7.4.4 mklib library carries (7 sections, 277 KB) never reaches the image. Fine for the
  simulator, but there is no source-level debugging of runtime code in CCS from an LNK6x
  image, and `--symdebug` is not a choice. Low priority; note in known.md.
- **Symbol table**: 1,290 vs 2,698 entries; TI keeps an `STT_SECTION` per *input* section
  (1,567 vs 283) and 120 NOTYPE locals. Cosmetic; `prof-map.py` reads the map, not the symtab.

## C. Linker performance

- **C1. `Strtab::add` (image.cpp:36)** does `s.find("\0" + t + "\0")` over the whole string
  table per symbol: O(symbols x strtab) = 28K x 1.3 MB on the harness. `sample` shows 85% of
  the run in `memchr`/`memcmp` under it. A scratch `std::unordered_map<std::string,u32>` beside
  the string: **2.85 s -> 0.022 s, byte-identical image** (the map returns the same offsets).
  TI's lnk6x takes 0.22 s on the same input, so LNK6x becomes 10x faster than the oracle.
  Kernels: 15 ms today vs TI ~100 ms.
- **C2.** After C1 nothing else is measurable at this size. Remaining quadratic shapes, worth
  knowing before a 50 MB link: the archive index is a `vector<pair<string,u32>>` scanned
  linearly per wanted name per pass (4,415 x wants x passes), `pull_symbol` the same,
  `out_index` is linear per lookup, and `eliminate`'s outer `for(;;)` rescans every section of
  every module per pass. A `map`/`unordered_map` over the index and a per-module "has exidx"
  flag close them.
- Memory: the harness link peaks well under 100 MB (whole-file `slurp` of an 18 MB archive
  plus copies of pulled members). Fine.

## D. Test gaps

- **D1.** No probe in `tests/ref` has a C++ throw, and none whose *LNK6x* image contains an
  unwind index is compared byte for byte (q07/q17/q18 are C; the corpus's C++ programs keep
  only TI's images). That is exactly how A1 survived 18 green probes, the 2026-09-20 review
  (which counted exidx relocations but never linked one) and the hello run. Add: a C++ probe
  with `throw`/`catch` (TI image checked in), and an `elfdiff` rule that flags an all-zero
  PROGBITS section.
- **D2.** Nothing runs an LNK6x image. `tools/c6747-three` links cpp11's object with TI's
  lnk6x on both simulator boxes; a sixth column "linked by LNK6x, run on windows-ccs55" would
  have caught A1 on the first program that throws and A2 on the first with a big table. The
  corpus (`tests/corpus.sh`) was last run 2026-09-20 - its `*ours*.log` predate every fix
  since - and `make test` fails at HEAD: q07/q17/q18 DIFF at 27,395 bytes against the pinned
  18,397 (`tests/known-differ.txt` not re-pinned, as known.md admits).
- **D3.** Probes worth adding, each of which this review had to improvise: the RLE probe
  (every byte value, a 300-run, a 70,000-run; TI's image is `back2/rle55.out`); a program that
  calls `remove`/`clock` (aliases); cl6x 7.4.4/8.2.2 `-O2` objects of the kernels (`back2/
  *.cl55.obj`) alongside ASM6x's; option order (`--heap_size` before and after the file);
  a `.bss` larger and smaller than `.text` (sieve); two translation units sharing weak/inline
  C++ definitions (cpp11 emits 1,421 weak symbols and no `SHF_GROUP` in the harness - how
  a second TU's copy is dropped has never been linked); and both linker versions, since the
  7.4.4/8.2.2 differences (A9, empty flags, `.far` zero_init) mean one set of references
  cannot serve both.

---

## Recommended order of work

1. **A1** - load bytes for `SHT_C6000_UNWIND` (one condition in `elf.cpp`), then **A10** sort
   by function address. Without this no exception is catchable; with it the index is at least
   correct for functions that have entries. Verify with a throwing probe run on the simulator.
2. **A3** - `EHTYPE` as `S + A` (one case). Unblocks the Compiler++ harness.
3. **C1** - hashed `Strtab`. One struct, 130x, byte-identical.
4. **A2** - the rle24 encoder: frequency escape, the 1..3 escape-run form, the two long forms.
   Decode with the model after every change; it is a silent memory clobber today.
5. **A4, A7, A8** - the allocation order (drop `.bss` first; sort key = floor size; `.stack`
   flags/align), and positional options. After these every kernel's `.text` and data land at
   lnk6x's addresses but for A6.
6. **A6** - `.TI.symbol.alias`, and the one-name-at-a-time archive pull (which also settles
   A11). Then the kernels' `.text` is byte-identical and the map differ is clean.
7. **A10** rest - `EXIDX_CANTUNWIND` synthesis and merging; **A9** - decide 7.4.4 vs 8.2.2,
   read the record/handler order with two probes, re-pin `known-differ.txt`.
8. **A5** - range checks now, trampolines later.
9. **D1-D3** alongside each: the bed cannot see any of the above today, and the simulator leg
   is what turns "byte-identical" into "runs".

Scratchpad artefacts (all under `.../scratchpad/review/`): `objs/` and `harness/h.O2.obj`
(inputs), `ours/` (LNK6x images and maps), `ti55/`, `ti74/` (TI's images, maps, ofd6x XML),
`back2/` (RLE probe images from both TI versions, cl6x -O2 objects and their TI images),
`scratch/` (the patched copy: EHTYPE, UNWIND bytes, hashed strtab), `relocs.py`, `sbs.py`,
`rledec.py` (the decompressor model), `sample.txt` (the profile).
