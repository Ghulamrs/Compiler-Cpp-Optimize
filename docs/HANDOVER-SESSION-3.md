# Handover to session 3: the allocator next

Fable 5.1, 2026-09-24, end of session 2 on branch `gcc-scheme`
(`Ghulamrs/Compiler-Cpp-Optimize`; `base` is the local original, never
pushed to). Session 1 left the infrastructure; session 2 built the first
of the speed passes on it, each landing alone, box-measured, and turned
the code the way the user asked: **each transformation a class, each
analysis a class, the level a cost model, the IR owning its invariants** -
in plain C++14, and reading as what it is.

Read first: `docs/OPTIMIZER-ARCHITECTURE.md` (the class diagram, kept in
step with the code), `docs/HANDOVER-SESSION-2.md` (the order of work, S1 to
Z4, which this continues), `docs/GCC-SOURCE-STUDY.md`.

## What landed, in order (all pushed; head `origin/gcc-scheme`)

| Commit | Step | -O1 .text | -O2 .text | bench -O1 | bench -O2 |
|---|---|---|---|---|---|
| `0f8eeb7` | base (session 1's head) | 600,302 | 854,782 | 1,181 | 841-845 |
| `76ba1a3` | `Costs` a class family: `SizeCosts`, `SpeedCosts` | identical | identical | - | - |
| `6588019` | S2: `Eh` edges, `Region`, `Live` | identical | identical | - | - |
| `e6332cc` | S1: the `Inliner` (cost, three budgets, badness order) | 600,302 (identical) | **739,406** | 1,164-1,181 (same binary) | **839** (q1 838, q3 843) vs 845 (839-846) |
| `e902874` | S3: `mir::Webs` a class; pinned occurrences split by copies | identical | identical | - | - |
| `adc37ba` | S4: `Loops` from the dominators, kept by the `Function` | identical | identical | - | - |

.text is Compiler++ on the Windows box, GNU route (clang + link.exe);
bench is Compiler++ interpreting `C:\o12study\bench.cpp`, median of 15
interleaved rounds with quartiles. "identical" means byte-identical
assembly on all 2,424 outputs of `tools/identical.sh` (every case and
every Compiler++ unit, both spellings, three levels) against the step
before. Every step: Mac run 476/0, emit 839/0, overload 30/30, names
287/0; S1 also the box's four Compiler++ suites at both levels, 258/258
against cl /O2 at both, and cxx1's 265 cases at -O0, -O1 and -O2.

### S1, the inliner (`Inliner.{h,cpp}`, `OptCosts.h`)

GCC's `ipa-inline` reduced to what the walker knows before it walks. Every
function of the unit is measured once (AST nodes), every call site listed
with the loops it sits in and its growth (callee size less the call it
replaces: 1 + arguments), and all sites decided in one pass in **badness
order** (growth over 2^depth, least first, ties in program order) against
three budgets from the costs: per site (`inlineGrowth(depth)`: 30 nodes,
doubling per loop level to three), per caller (`callerGrowthPercent` 100 of
`max(size, largeFunction = 2700)`), per unit (`unitGrowthPercent` 40). The
walker asks `allows(site)` as it reaches each call and gets the decision
made. A funclet-cut caller reserves `largestFrame()`, the largest frame of
any site admitted.

What was tried and did not land: **15 nodes per site** (GCC's own number,
but a node here is about half a GCC insn) gave 712,302 and a bench of 856
(853-858) against 841 - smaller, slower, refused. **-O1 inlining where
growth <= 0** grew Compiler++'s -O1 .text by 783 bytes: `SizeCosts::inlines()`
is false until a cost in bytes (Z1) can tell a body smaller than its call.
**Callees with a goto label or a switch** cannot be walked in place: the
parser names their labels once per function (`label("case", c->id())`,
`userLabel`), and a body walked twice names them twice - found by cxx1's
own cases at -O2 on the box (`goto-legal-jumps`, `nested-parse-state`),
not by anything on the Mac. To inline them, the walker must number case
and user labels per walk.

### S2, exception edges (`OptCore.h`, `Walker.{h,cpp}`, `Optimizer.{h,cpp}`)

The walker tells the optimizer each `Region` a landing pad covers
(`exceptionRegion(begin, end, target)`: the call-site rows on the Itanium
targets, the try range with its resume label under funclets); `Flow::build`
gives every call inside one an `Edge::Eh` to the target's block. **The call
stays in its block** - GCC splits there, but a split took a pushed constant
from `forward-values` on 29 outputs - and `Edge::at` says which entry the
edge leaves at. Liveness is a value, `Live` (`step`, `join`); `solve` and
every backward walk in the passes step the one `Live` and call
`Flow::joinPads(b, k, live)` at each entry, so what a pad reads is live at
the call and the call's own effects kill what it clobbers. Reaching
definitions still take an Eh edge from the block's end (a safe
over-approximation; `ReachingDefs::solve` could snapshot at `at`).

### S3, pinned occurrences (`Mir.h`, `MirWebs.cpp`)

`mir::Webs` is a class over the `Function`: `splitPinned` inserts a whole
copy (`mov %r, %r`, eight bytes - a four-byte one zero-extends) before an
instruction that reads a register it does not name (a call's arguments,
`idiv`'s rdx:rax, `rep movsq`'s three) or wants one by name (a shift's
`%cl`), and after one that writes a register it does not name that is live
after (a call's rax); a `ret`'s implicit reads stay. Then `build` (the
phases `makeDefinitions`, `joinUsesToDefinitions`, `renameToPseudos`),
`assign(homes())`, `dropSelfCopies`. Over Compiler++ at -O2 the pseudos
went from 33,395 to 55,826. The pass now says it destroys `kPropFlow`.

### S4, loops (`OptLoops.{h,cpp}`, `OptFunction.h`)

`Loops`, from the dominators, cached by `Function::loops()` and dropped on
`buildFlow()`. Tried as `promote-locals`' weights: 89 outputs changed for
2 bytes either way; not taken (the rule: a behaviour change measured at
nothing is reverted). The allocator will weigh by them.

## The box, as set up this session

`C:\cxxopt` on the Windows box, this branch's own directory: `C++`
(a git clone fed by `cxxopt.bundle`), `Compiler++` (the export),
`w\<tag>-<level>\` (each build), and the scripts `sync.cmd` (bundle ->
tree -> `msvc\build.cmd`), `opt.cmd TAG O1 O2` (Compiler++ built, suites,
258 cases against `C:\cxx1dev\w\cl-O2`, best-of-five bench), `optmasm.cmd`
(the same with `-masm=masm` through RIDE's `masm.exe`), `cases.cmd O0 O1
O2` (cxx1's cases), `ab.cmd N BUILD...` (interleaved rounds; `cl-O1`/`cl-O2`
read from `C:\cxx1dev\w`). Copies of all of them are in this session's
scratch; they are not in the repository. Never write into `C:\cxx1dev`.

The Mac side: `~/cxxopt-build/tree` is the build of the checkout
(rsync'd, never build in `~/Documents`), `base/cxx1.exe` the session-1
reference, and a `.text` proxy that needs no box - every Compiler++ unit
assembled by `clang -target x86_64-pc-windows-msvc` and the `.text`
sections summed with COMDAT sections counted once (a 30-line Python; the
proxy sits 110-140 KB under the box's linked figure, the CRT, and moved
with it every time). Iterate on the Mac by the proxy, judge on the box.

## Next: S5, the register allocator, and what it needs

Everything S5 was to wait for is in place except def-use chains, which
have no client yet and were not built (an analysis with no client is
scaffolding; build `DefUse` in `OptDataflow` when the first pass asks).

- **`MirAlloc.cpp`, a class `Allocator`** (a `Pass` after `webs`; `webs`
  then destroys `kPropPhysical`, the allocator provides it). Interference
  from `Live` over pseudos - `Flow::solve` already tracks 64 registers in a
  `RegSet`, so pseudos need their own liveness (a bitset per pseudo, or
  intervals from a linear order); Chaitin-Briggs with conservative
  coalescing of the copies S3 inserted (they are the preferences); costs
  from `Costs`: references weighted by `Loops::depthOf` at -O2, by
  encoding bytes at -O1 (Z1); a pseudo live across a call prefers a
  callee-saved register, charged its save; spills below `frameBase()`;
  `finish-frame` as today. The funclet rule from the IR doc: a pseudo read
  or written in any funclet stays in its slot.
- **`Function::promotable` and landing pads.** A local kept in a
  callee-saved register needs the unwinder to restore it into the pad:
  the prologue's saves (`calleeSaves`) must carry CFI (`.cfi_offset`,
  and the MS unwind codes) before the frame passes run in a function with
  landing pads. Check what `Spelling::calleeSaves` emits on each route
  first; the S2 edges make the liveness right once it does.
- **Parameters and temporaries (S6, S7)** are what makes the allocator
  pay on Compiler++: the walker stores every value to the frame. S3's
  copies give the shape: a parameter is a copy out of its argument
  register into a pseudo; a temporary is a pseudo where the walker now
  uses `push`/`pop` and rax.
- **Measure** as this session did: the proxy on the Mac for every
  variant, the box for the candidates; the base for every comparison is
  the step before (`S=$scratch/s2/box/run-s1c.sh` shape: sync, `opt.cmd
  TAG O1 O2`, `cases.cmd O0 O1 O2`, `ab.cmd 15 base-O2 TAG-O2`).

## Also open, smaller

- Inline callees with a `switch` or a goto label: number case and user
  labels per walk in the walker (`Walker::visit(Switch)`, `userLabel`).
- `-O1` inlining needs Z1's bytes cost to say what is smaller than a call.
- `ReachingDefs` at `Edge::at` (precision only).
- The two rebuild asymmetries and the opcode-table quirks from the
  session-2 handover are still unmeasured one-liners.
- `Loops` as `promote-locals`' weights: measured neutral, kept out; comes
  back with the allocator.
