# Why cxx1i's code is bigger and slower than cl's

Investigation by Fable 5.1, 2026-09-22, on the Windows box. Sixteen units of
the Compiler++ project as RIDE 4.0 ships it, both sides assembled and linked
identically, all programs byte-identical in output.

Evidence: box scratch `C:\fresh\study\` (cl `-FA` listings and cxx1i `-S`
listings for all 16 units at both levels, dumpbin headers/symbols, relinked
exes with .map files, disassembly and .rdata dumps). Nothing in any repo was
modified.

All figures re-derived from the exes' section headers: cl /O1 .text 0x7ECB4 =
519,348, .rdata 0x27800 = 161,792; cxx1i -O1 .text 0xA220E = 664,078,
.rdata 0x35D02 = 220,418.

## Part 1 - the data gap (.rdata +58,626): one cause, fully accounted for

| what reaches .rdata | cl /O1 | cxx1i -O1 | delta |
|---|---:|---:|---:|
| RTTI (`.rdata$r`: `??_R0..R4`) | 9,464 | **74,870** | **+65,406** |
| unwind + EH tables (.xdata) | 44,803 | 50,636 | +5,833 |
| strings + vtables (.rdata) | 21,779 | 25,973 | +4,194 |
| CRT/STL libs (by subtraction) | ~85,700 | ~68,900 | -16,800 |
| total | 161,792 | 220,418 | +58,626 |

**1. RTTI is emitted once per translation unit, non-COMDAT.** `Masm.cpp:894`
writes every class's `??_R0/R1/R2/R3/R4` into one plain `.rdata$r SEGMENT`
per unit; dumpbin shows 16 sections, 74,870 bytes, none COMDAT, so the linker
keeps all 16. Proof in the exe: 628 `.?AV...` type-name strings of only 58
distinct, each present exactly 16 times. cl emits each as its own selectany
COMDAT (1,341 sections folding to 318). **Worth ~65 KB - more than the whole
data gap.** cxx1i pulls ~17 KB *less* CRT read-only data, which masks it.

**2. String literals are not pooled.** `.CONST` gets one label per *use*:
in main.asm `$_L_str_0` and `$_L_str_1` are both `"string::at"`. Across the
16 listings: 1,804 literals / 13,840 bytes, 844 / 8,565 unique. cl pools with
`??_C@` COMDATs. **Worth ~5 KB.** Measured.

**3. Unwind/EH tables.** `__CxxFrameHandler3` with rbp frames (520 of 1,222
unwind records; cl: 5) against cl's compressed `__CxxFrameHandler4` FuncInfo.
**Worth ~6 KB.** Measured; the fix is large and not recommended.

## Part 2 - the code gap (.text +144,730; run 4.3x)

Where the bytes are, from the .map files:

| code in the exe | cl /O1 | cxx1i -O1 | delta |
|---|---:|---:|---:|
| the program's own functions | 224,550 | 436,343 | **+211,793 (+94%)** |
| std:: instantiations (each compiler's own) | 98,920 | 66,269 | -32,651 |
| CRT/STL .lib code | 194,153 | 160,569 | -33,584 |

**The true code-quality gap is +94% on user code**, hidden to +28% by cxx1i's
smaller library and CRT pull-in. 557 functions match by name; cxx1i's average
1.83x cl's - and that flatters cxx1i, since cl's user functions contain
inlined vector/string bodies.

**4. Every call is wrapped:** `xor eax,eax ; sub rsp,32 ; call ; add rsp,32`.
`X86_64Linux.cpp:1155-1170` (shared by the Windows target) emits
`mov $0,%rax` for every call though only SysV varargs need AL and Windows
never does, and allocates/frees the 32-byte shadow space per call. Exe counts:
`sub rsp,20h` 9,398 vs cl 699; `xor eax,eax` 11,793 vs 342. **~75-85 KB, over
half the gap.** cl allocates the outgoing area once in the prologue.

**5. No address-mode folding.** Member access is
`mov rax,r12 ; add rax,121 ; movzx rax,BYTE PTR [rax]` where cl writes
`cmp BYTE PTR [rcx+113],bl`. `add` 28,341 vs 7,040; frame references
`[rbp+..]` 54,351 vs cl's 9,603+14,042; `movsxd` 8,929 vs 961.
**Estimate 40-60 KB** plus real run time.

**6. Branch and boolean shape.** `cmp reg,0` 3,334 times vs cl's 3 (cl uses
`test`, a byte shorter); conditions materialised then re-tested; every
if/else arm ends in `jmp end`. **Estimate 15-20 KB.**

**7. Register allocation is off in every function with a `try` - including
the one the benchmark runs.** `Optimizer.cpp:975` requires `heldLsda_.empty()`.
At -O2 that is 296 of 6,334 PROCs but **28.7% of all instruction lines**.
`VM::run`, the interpreter loop and the entire bench workload, has a `try` and
uses **no** callee-saved register at either level; `this` is reloaded from
`[rbp+3656]` for every member access. This is also why -O2's three extra
registers buy so little: frame references fall only 54,351 -> 49,037.

**8. The per-step path calls the library out of line; cl inlines it.** In
`VM::run`, before dispatch, cxx1i runs 176 instructions with **12 calls**
(`vector::empty/back/operator[]/size`, `VM::failed`, string constructions),
each carrying the item-4 wrapper; cl's loop calls only `VM::pop`. Across all
units, calls to std::string/vector/map members: 6,549 vs 972. `VM::run` is
20,739 bytes vs cl's 9,252.

**9. `switch` is a linear compare chain.** `Walker.cpp:168` emits one
`cmp rax,N ; je` per case in source order (not even sorted). `VM::run`'s
64-way dispatch averages ~32 compare-and-branch pairs per interpreted step;
cl builds a compare tree ~6 levels deep.

**Run-time attribution (inferred, not profiled - no sampling profiler on the
box).** 50M steps: 34 ns/step under cxx1i vs 7.8 under cl. Items 7, 8, 9 are
all on that path and compound. Roughly a third each to 7+5 (memory traffic),
8 (calls), 9 (dispatch) - a judgement, not a measurement.

**Not the cause:** cxx1i's own std library is smaller than cl's instantiations
and its CRT share is smaller. Nothing in the .text gap comes from the library
side. The levels behave as designed: -O2's +2.6% is exactly five registers vs
two plus unrolled copies and alignment; there is no second axis yet.

## Part 3 - recommendation, ordered by value per effort

| # | item | where | worth | effort | level |
|---|---|---|---|---|---|
| 1 | Emit each RTTI record as its own selectany COMDAT | `Masm.cpp` ~885-955 | **-65 KB .rdata** (measured); .rdata lands *below* cl's | ~1 day | both |
| 2 | Drop `xor eax,eax` before calls except SysV variadic | `X86_64Linux.cpp` ~1155 | **-16 to -20 KB .text** (measured) | hours | both |
| 3 | Allocate the shadow/outgoing area once in the prologue | call emission + frame bookkeeping + Windows prologue | **-55 to -65 KB .text** (estimate) | several days; touches unwind | both |
| 4 | Address-mode folding as an IR peephole (family of `fuseLeas`); `cmp r,0` -> `test r,r` | `Optimizer.cpp` | **-40 to -60 KB** (estimate) + run time | medium | both |
| 5 | Register allocation inside functions with handlers | `Optimizer.cpp:975` | the 28.7% that gets nothing today, including the hot loop; **largest run-time lever short of inlining**; est. 20-30% of bench time | medium-high | both; where the 2-vs-5 axis finally reaches code that matters |
| 6 | Jump table for dense switches, compare tree otherwise | `Walker::visit(Switch)` + spelling | ~32 branch pairs -> 1 indirect jump per step; size-neutral | 1-2 days | both |
| 7 | String pooling + `??_C@` COMDATs | literal emission | -5 KB (measured) | ~1 day | both |
| 8 | Inlining of small functions - library accessors first | the walker (AST level) | **the rest of the run-time gap**: 6,549 accessor calls become loads | high | **the real -O1/-O2 axis** |
| 9 | Branch/boolean shaping; fall through instead of `jmp end` | walker `genTruth` + jump threading | -15 to -20 KB (estimate) | medium | both |

**A fair target.** After 1-4 (the cheap size items): .text ~535-560 KB, i.e.
**+3% to +8% of cl /O1** (from +28%), and .rdata ~150 KB, **below cl's 162**.
Those four barely move run time. After 5, 6 and 8: **about 2x cl, ~800-950 ms**
on the bench (from 1,691) - a judgement from reading the per-step path.

**The ceiling, plainly.** The remaining ~2x is structural. The walker is a
one-register stack machine (results in rax, temporaries pushed or spilled, one
function at a time) and the optimizer rewrites that stream afterwards.
Peepholes can fold addresses and delete wrappers; they cannot select
instructions from the expression tree, keep values in registers across
statements, schedule, or see across functions. cl's last 2x is SSA-level
global register allocation, instruction selection over the tree, and
cross-function inlining. Parity means the walker producing a register-transfer
IR with virtual registers - not more passes over spelled text. cl's
vectorisation buys nothing here (its own /O2 is only 4.9% faster than /O1).

**What not to do.**
- Loop unrolling (measured at nothing), loop-head alignment tuning,
  vectorisation: no loop kernel here pays.
- More callee-saved registers or better promotion heuristics *without*
  lifting the `try` exclusion.
- FuncInfo4/`__CxxFrameHandler4` or dropping rbp frames: ~6 KB, large risk.
- Attacking the library or CRT share: cxx1i is already 66 KB smaller there.
- `cmp x,0`/`movsxd` peepholes alone: ~7 KB; fold into item 4.

## Side findings

- cxx1i mangles its `std::string` as `Vstring@std@@` and private virtual
  methods with `U` where cl writes `E`. Harmless inside a cxx1i-built
  program; it means cxx1i and cl objects cannot be mixed, which is why run
  time could not be attributed by swapping one unit.
- Member offsets differ between builds (`currentMethodIsConst` +121 vs +113):
  cxx1i's string/map are laid out differently from MSVC's. Expected.
- `-version` prints "Version 1.2, sealed 10-09-2026" from `src/Version.h`, not
  the build's vintage; the objects match, so it is the measured compiler.
