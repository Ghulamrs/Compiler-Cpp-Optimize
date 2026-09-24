# Handover to session 6: locals are the allocator's, temporaries are not yet

Fable 5.1, 2026-09-24, end of session 5 on branch
`claude/fable-5-1-background-work-kguwml`, from `3776b0b` (session 4's head
plus the coordinator's Windows bench tools). A Linux container again: no
Mac, no Windows box, no Compiler++ tree. Everything below was measured
here; the last section says what the box has to run.

Read first: `docs/OPTIMIZER-ARCHITECTURE.md` (kept in step: `mir::Locals`,
the allocator's preserved registers and demotion, calls' exact reads),
`docs/HANDOVER-SESSION-4.md` (S5), `docs/HANDOVER-SESSION-3.md` (the order
of work, S6 and S7 there).

## What landed, in order

| Commit | Step | What it is |
|---|---|---|
| `1f53a77` | S6a | A call reads the argument registers its plan filled: `Optimizer::callArguments`, `Instr::args`, `effectsOf` reading them. No copy into a register the call never reads; the `xor %eax, %eax` before a non-variadic call is dead and goes |
| `565a25f` | S6b | `locals` (`mir::Locals`) renames every promotable scalar local to a pseudo; the allocator offers preserved registers charged their save, reserves the level's count for the heaviest locals that need one, and gives a local its slot back where no register can hold it. `promote-locals` and `promoteLocals` are gone |

Parameters are covered by S6b: a parameter's store into its slot is a copy
out of its argument register into a pseudo, and the allocator coalesces it
into that register where nothing keeps it out - `sum(const int *a, int n)`
now keeps `a` in rdi, `n` in edx, its two loop locals in eax and ecx, and
saves nothing, where S5 saved four callee-saved registers for the same
four values.

### What S6a found

Every call's effects read the convention's whole argument set - six
integer registers, eight SSE, rax - because nothing had told the optimizer
which of them a call used. Two costs: `splitPinned` copied a value into
every argument register live at any call, and once the allocator could
colour a pointer straight into rdi, the copy into rcx that a two-argument
call never read stayed as a real `mov`; and the `xor %eax, %eax` before
every call for the variadic count in al was live into every non-variadic
call. The walker knows the plan (`intsUsed`, `ssesUsed`, whether the
callee is variadic) at the one place it issues a planned call; a call
issued by hand (`terminate`, a funclet's helper) says nothing and keeps the
whole set, so the default is the safe one.

### What S6b needed beyond S5

- **A local has no home.** S5's fallback - the whole function back to its
  homes - has nothing to say for a promoted local. `demote` sends the
  failed node's slot members back to their slots in the stream
  (`Webs::assign` with `kToSlot`) and the graph is built again; only a web
  pseudo left without a register falls back to the homes, and no function
  in the cases does.
- **Preserved registers.** Those the stream never names are taken out of
  the physical liveness the interference reads - their life from entry to
  return is exactly what a save and a restore remove - and one taken is
  saved by the prologue and restored before each return (`addSaves`,
  `insertRestores` shared out of `promoteLocals`), at a slot below
  `frameBase()`, so `finish-frame` sizes the frame as before.
- **Who gets them.** The first version handed a fresh preserved register
  to whichever needy node select reached first, and on Windows at -O1
  (a seven-register palette, two preserved allowed) that demoted a loop's
  pointer while two lighter locals took the registers; and a merged node's
  web references counted toward the save a slot alone never earned.
  `reserveFresh` ranks the nodes no caller-saved register can hold by
  their slot accesses in `minWeight`'s loop-weighted unit
  (`Costs::loopWeight`) and marks the top `Costs::registers()` - what
  `promoteLocals` did, with `Loops`' depth in place of its count of labels
  and backward jumps, which weighed a block after a loop as inside it.
- **Leave edges.** A local is kept live across one. No code generator jumps
  through a register, so a whole function has none; a funclet-cut stream
  does, and the rule costs nothing.
- **`removeDead`** knows the `movl %eax, %eax` a coalesced four-byte store
  leaves, as it knew `mov %eax, %eax`.

### Where the bench stands, and why `virtual` is not a number

`bench.cpp` (fib, sieve, matmul, isort, hash, virtual; nine interleaved
rounds, medians, checksums equal throughout) under cxx1's own link:

| | g++ -O2 | S5 | S6a | S6b |
|---|---|---|---|---|
| fib | 3 | 14 | 14 | 14 |
| sieve | 61 | 90 | 88 | 87 |
| matmul | 10 | 62 | 63 | 57 |
| isort | 9 | 37 | 34 | 37 |
| hash | 232 | 351 | 343 | 334 |
| virtual | 155 | 203 | 220 | 221 |
| total | 479 | 757 | 765 | 757 |

Nothing outside the noise, and that is the expected shape: these kernels'
locals were already in `promote-locals`' registers at -O2, so what S6
buys them is saves, not loads. `virtual` is one indirect call in a loop
whose time swings from 150 to 220 ms with code placement: the S6a and S6b
assemblies linked alike with `c++ -no-pie` measured 151 and 188, and with
the two `area` methods aligned (`.p2align 4`) 206 and 206. Do not read
that row. The box's Compiler++ bench is what judges speed.

`fib` at 14 ms against g++'s 3 shows the next step: its operand across the
recursive call is `push %rax; sub $8, %rsp; ... add $8, %rsp; pop %rdi` -
the stack machine's temporary, which S7 makes a pseudo.

## Measured here

| Check | S6a | S6b |
|---|---|---|
| every case at -O1 and -O2, host, run and diffed (asserts live) | 574 / 0 | 574 / 0 |
| `bash tests/run.sh` | 476 / 0 | 476 / 0 |
| `tests/emit.sh` (the -O0 golden) | 0 of 839 changed | 0 of 839 changed |
| `tests/overload.sh`, `tests/names.sh` | 30 / 0, 287 / 0 | 30 / 0, 287 / 0 |
| `tools/identical.sh`, `LEVELS=0` | 574 / 0 differ | 574 / 0 differ |
| `tools/identical.sh`, all levels, against the step before | 969 of 2,296 differ; 32,287 lines gone, 12,772 come | 869 differ; 60,097 gone, 52,179 come |
| outputs with more instructions than before | 5 (one to four) | 12 (one to four) |
| `tools/comment-lines --count` | 63 | 63 |

The size proxy (every case emitted at a level, assembled by clang for its
target, `.text` summed):

| | S5 (d679b34) | S6a | S6b |
|---|---|---|---|
| x86_64-linux -O1 | 352,438 | 332,930 | 319,746 |
| x86_64-linux -O2 | 417,997 | 399,170 | 386,851 |
| x86_64-windows -O1 | 322,107 | 320,032 | 319,773 |
| x86_64-windows -O2 | 383,117 | 380,063 | 379,487 |

The allocator over the cases (S6b): x86_64-linux -O2 5,533 locals
promoted, 5,390 kept in a register, 143 given back their slot, 1,225
registers saved; -O1 5,596 / 4,599 / 997 / 536; x86_64-windows -O2
5,110 / 2,758 / 2,352 / 2,758; -O1 5,194 / 731 / 4,463 / 731. No function
fell back to its homes on either target at either level.

## What the box must run to judge it

From `C:\cxxopt`, as sessions 3 and 4 laid it out:

1. `sync.cmd` with a bundle of this branch's head, then `msvc\build.cmd`.
2. `opt.cmd s6 O1 O2`: Compiler++ at both levels, its four suites, 258
   cases against `C:\cxx1dev\w\cl-O2`. The gate: 258/258 and green.
3. `cases.cmd O0 O1 O2`: cxx1's cases on the box - this is where the
   Windows funclet, RTTI and `try` shapes first *run* under the allocator's
   preserved registers; nothing here executed Windows code.
4. `.text` of Compiler++ at -O1 and -O2, GNU and MASM routes. Session 3's
   figures: 581,934 / 720,782 (GNU), 585,598 / 723,198 (MASM). The proxy
   says -O1 falls about 9% and -O2 about 7% against S5's numbers.
5. `ab.cmd 15 base-O2 s6-O2` and `ab.cmd 15 base-O1 s6-O1` (session 3:
   734 and 1078 ms). The rule stands: a behaviour change measured at
   nothing is reverted, but a size fall with the bench unchanged is a
   result.
6. `tools\windows\bench.cmd` for the kernels against cl /O2.
7. On the Mac, `tools/identical.sh` with the Compiler++ tree, for the
   "no output with more instructions" check over its units.

## Next, in order

- **S7, temporaries as pseudos**: a `push` whose `pop` pairs with it, and
  the `sub $8, %rsp` beside it, become a copy into a pseudo and a use; the
  register pressure this raises is what will first need a real spill slot
  below `frameBase()` for a web pseudo (today: the homes).
- The inliner's callees with a `switch` or a goto label (session 3's item).
- Z1's bytes cost, for -O1 inlining and for the register order.
