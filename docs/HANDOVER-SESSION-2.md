# Handover to session 2: the speed passes, then the size costs

Fable 5.1, 2026-09-24, end of session 1 on branch `gcc-scheme`
(repository `Ghulamrs/Compiler-Cpp-Optimize`; `base` is the local original,
never pushed to). Session 1 left the infrastructure below and changed
nothing the compiler emits. Session 2 builds the passes on it: **speed
first, then size**, in the order here, each step landing alone.

Read first: `docs/OPTIMIZER-ARCHITECTURE.md` (the classes and the
pipeline), `docs/GCC-SOURCE-STUDY.md` (why they have the shape they have),
`docs/GCC-SCHEME-2026-09-24.md` (the gap table, the pass list M1-M18, the
traps in section 5 - all still true).

## What session 1 left

Built, gated byte-identical on 2,424 outputs and green on the four Mac
suites, every commit pushed:

- `opt::Opcode` table (`OptTable.{h,cpp}`) - the one place an instruction
  is described.
- `opt::Edge`/`opt::Block`/`Flow` with predecessors, edge kinds (`Eh`
  declared, unmade), dominators on request.
- `opt::Function` and `opt::Costs` - the state a pass takes, and the level
  as a cost table.
- `Flow::live()` solved on demand with `solutionsDirty`; `ReachingDefs`
  as a problem, consumed by `webs`.
- `Pass`/`Group`/`PassManager`, properties (`kPropFlow`, `kPropPhysical`),
  start TODOs, the pipeline in `OptPipeline.cpp`, dumps via
  `CPP11_DUMP_MIR`.

Not built (documented stubs, see the architecture doc section 7): EH
edges, def-use chains, any client of the dominators, the inliner's and the
allocator's cost fields.

## The gate, from here on

Session 1's gate was byte-identity. From the first step below it becomes
what `docs/OPTIMIZER-IR.md` says a stage ships on: **suites green on all
boxes, and neither level's size nor speed worse** - with byte-identity kept
for `-O0` throughout (it is the reference the other two are diffed against),
and `tools/identical.sh` run at `-O0` only, or with a step's changes
switched off, to prove the reorganization parts of a step are still
neutral.

Per step, on the Mac: `tests/run.sh` (476), `tests/emit.sh` (839),
`tests/overload.sh` (30), `tests/names.sh` (287), from a build outside
`~/Documents` (`~/cxxopt-build/tree`; never build in the checkout). On the
Windows box, with the user's yes first (name the boxes and the time; the
box run has a two-hour cap and every command a thirty-minute one):
Compiler++ built with `-masm=masm` through RIDE's `masm.exe`/`link.exe`
and its four suites, 258/258 against cl /O2 at -O1 and -O2, cxx1's cases
at all three levels through masm and through ml64, `.text` of Compiler++
at -O1 and -O2 (`tools/crossbox`), and the bench with interleaved rounds
(best of five). The reference numbers, from `0b02c54`: .text 603,934 /
856,878 (masm route, -O1 / -O2), 600,302 / 854,782 (GNU route); bench
1,216 / 831 ms; cl /O2's .text 623,092.

## Speed first

### S1. The inliner's cost and budget (study step 1; GCC `ipa_inline`)

Where: `X86_64Linux::inlineTarget` / `small()` (`X86_64Linux.cpp` around
line 1811) decide today with no cost; `Costs` gets the parameters.
Prerequisite: none.

Build: a per-callee size summary (the instruction count of its own walk,
cached in `bodies_`; or the AST node count as a first proxy), a per-site
growth = callee size - call sequence size, and three limits read from
`Costs`: per site (`maxInlineInsns`, GCC's auto 15 / single 70 shape), per
caller (growth as a percentage of the caller's own size, GCC's
`large-function-growth` 100%), per unit (`inline-unit-growth` 40%). At
-O2 (`forSize == false`) inline while all three hold; at -O1 only where
growth <= 0 (GCC's `max-inline-insns-size` rule, cl's /O1). Keep `small()`'s
safety conditions (no landing pads, no variadic, no stack args). Give each
site its own slots below `inlineTop` rather than one shared region, so
that S5/S6 can promote them (`Optimizer::inlineBegin`).

Verify: suites; **-O2 .text** must fall from 856,878 toward cl's 623,092
with the bench not slower; -O1 .text must not grow.

### S2. EH edges in the CFG (study step 3; GCC `lower_eh` + `EDGE_EH`)

Where: the walker tells the Optimizer each try range and its pad (new
events beside `stateLabel`); `FlowOf::build` gives every `call` inside a
range an `Edge::Eh` to the pad's block; `solve` treats an `Eh` edge as GCC's
LR confluence does (the call-clobbered registers die across it, the rest
flow into the pad). Prerequisite: none (the kind exists).

Then: `Function::promotable` no longer needs `!hasLandingPads()` for the
passes that only read liveness; `removeDeadStores` may run in cut functions
once it is the backward problem (S10).

Verify: the exception cases especially (`try-*`, `throw-*`, `rethrow`,
`stdexcept`), Compiler++ against cl, byte-identical output on functions
without a `try`.

### S3. Pin an occurrence, not a web (study step 4)

Where: `MirWebs.cpp`, before `unite`. A use or def that must be in a
particular register (a shift count in `%cl`, `idiv`'s rdx:rax, an argument
register at a call, `rep movsq`'s three) is split off with a copy to or
from a fresh pseudo, so the web is free and only the copy is pinned.
Prerequisite: none.

Verify: byte-identical output with `assign(home)` still in place (the
copies coalesce back); the pseudo count and the pinned count on the IR
doc's case (5,142 of 6,412 pinned today) reported in the commit.

### S4. Def-use chains and dominator clients (GCC `DF_CHAIN`, `dominator`)

Where: `OptDataflow.{h,cpp}` - `DefUse` built from `ReachingDefs`
(`df-core.cc`'s own advice: build chains from RD), giving each use its
defs and each def its uses. `Flow::dominators()` gets its first client.
Prerequisite: S2, S3.

Verify: no output change (an analysis); a `CPP11_DUMP_MIR` section that
prints the chains for a case.

### S5. The register allocator (study step 6; GCC `ira` + `lra`, reduced)

Where: new `MirAlloc.cpp`; a pass `allocate` in the pipeline after `webs`,
replacing `promote-locals`; `webs` destroys `kPropPhysical`, `allocate`
provides it, and every pass between says which it can take.
Prerequisite: S2, S3, S4.

Build: interference from liveness over pseudos; Chaitin-Briggs with
conservative coalescing (copies as preferences, as IRA does, not a
separate pass); costs from `Costs` - references weighted by loop depth at
-O2, by encoding bytes at -O1; a pseudo live across a call charged the
save/restore of a caller-saved register so it prefers callee-saved;
spills to new frame slots below `frameBase()`; the frame finished by
`finish-frame` as today. The funclet rule: a pseudo read or written in
any funclet of the function stays in its slot; everything else is a
candidate (the IR doc's open question, decided the simpler way).

Verify: suites; loops.cpp kernels; bench; both .text figures (-O1 may not
grow). Expected: about 2x on the loop kernels, small on Compiler++ until
S6/S7.

### S6. Temporaries as pseudos (study step 7; GCC expand/`ter`)

Where: a reader step before `webs` (`MirWebs.cpp` or a new `MirRead.cpp`):
a `push` whose `pop` pairs with it becomes a `mov` into a pseudo and the
pop a use; pushes that escape (a call with stack arguments, the
scratch-register carry) stay. Prerequisite: S5.

Verify: suites; bench; .text.

### S7. Parameters as pseudos (study step 8)

Where: `X86_64Linux.cpp` "Parameters into their slots" (about line 1606)
gives the Optimizer the slots; the reader turns an unaddressed parameter's
home-slot store into a copy into a pseudo and drops the slot when nothing
addresses it (Windows: the shadow space stays, its stores go).
Prerequisite: S5.

Verify: suites; bench on call-heavy code (this is where cl's lead is);
.text.

### S8. Value numbering over the dominator tree (study step 9; GCC `fre`, `dominator`)

Where: `OptValues.cpp`: `Forward`'s table inherited down the dominator
tree instead of reset at every label; a hash of (opcode, value ids) marking
a recomputation as a copy of the earlier result. Prerequisite: S4.

Verify: suites; .text; bench.

### S9. Tail calls (study step 13; GCC `tail_calls`, `-foptimize-sibling-calls`)

Where: the walker marks a call in tail position (no destructors pending,
no stack-argument area larger than the caller's; Windows: no `try` in the
caller); a pass turns epilogue+ret after it into the epilogue then `jmp`.
-O2 by cost; -O1 where smaller. Prerequisite: S5, S7.

Verify: suites (exceptions, destructors); bench.

### S10. Loop-invariant motion (study step 15; GCC `loop2`/`lim`), and the backward DSE

Where: new `OptLoop.cpp` - natural loops from the dominators (a back edge
`b -> h` with `h` dominating `b`), a preheader, invariants hoisted when
their operands are; `Costs` says whether the register pressure it raises is
worth it at -O1. `removeDeadStores` becomes the backward set-union problem
in `OptDataflow` (stores as gens, reads as kills), so it runs in cut
functions. Prerequisite: S4, S5.

Verify: loops.cpp kernels; bench; .text.

## Then size

### Z1. Costs by encoding bytes

Where: `Costs` gets `int bytes(const Instr &)` (an encoding-length
estimate per opcode form: REX, ModRM, displacement and immediate widths)
and the allocator, the inliner and if-conversion read it when `forSize`.
Prerequisite: S5 (the first client).

Verify: -O1 .text on Compiler++ (603,934 masm route) must fall; bench at
-O1 not worse than today's 1,216 ms.

### Z2. The -O1 gates

Where: every pass that can grow code asks `Costs` before doing so: the
inliner (growth <= 0), LIM (no spill), tail calls (shorter), cmov (shorter
than the branch). No pass is switched off by level. Prerequisite: Z1.

Verify: -O1 .text; the `-O1`/`-O2` pair measured on the box, interleaved.

### Z3. Crossjumping (study step 16; GCC `jump2`, `-fcrossjumping`)

Where: `OptDead.cpp` or a new pass after `allocate`: identical tails of
blocks that end at the same target merged into one, the others jumping to
it. Prerequisite: S5.

Verify: -O1 .text (exception-heavy epilogues especially).

### Z4. If-conversion by size (study step 12; GCC `ifcvt`)

Where: new `OptIfcvt.cpp`: a diamond whose arms are one move each becomes
`cmov`; a `setcc` diamond likewise; at -O1 only when `bytes()` says it is
shorter, at -O2 when both arms are cheap. Prerequisite: S8, Z1.

Verify: suites; -O1 .text must not grow; bench.

## Also on the list, when they come up

- **Volatile kept** (study step 11): a qualifier bit on the type
  (`ParserType.cpp:1322`), carried to the walker's loads and stores, marked
  opaque in `Effects`; `Driver.cpp`'s -O0 fallback goes. Before S10's DSE
  gets stronger.
- **The opcode table's quirks** (`OptTable.cpp`): `addq`/`subq` as
  arithmetic, `sal`/`rol`/`ror` as RMW, `testl`/`testq`/`cmpq` as
  explicit-only. Each a one-line change; each changes output; each to be
  measured on its own.
- **The two rebuild asymmetries** in the pipeline (`rounds` drops unnamed
  labels, the loops do not; `webs` rebuilds once more): make them uniform
  and measure - a label dropped later could join blocks the passes then see
  whole.
- **arm64** (study step 17): its reader and description, the same passes.

## Things found in the base, not fixed (report, do not fix, was the rule)

1. `OptDead.cpp` `coalesceCopies` patches `f.effects[p].writes` by hand
   after renaming the producer's destination, rather than recomputing the
   entry's effects. Today equal to `effectsOf` (the renamed operand is a
   destination, so `reads`/`wide`/`partial` are unchanged), but a later
   change to what a destination contributes would leave this one site
   stale. Not a miscompile now.
2. `OptTable.cpp`'s record of the old lists shows `testl`, `testq`,
   `cmpq`, `cmpb`, `testb` are not "explicit-only" while `cmp`, `test`,
   `cmpl` are: `wideOf` therefore treats a 4-byte `testl %eax, %eax` as
   reading rax wide, which only makes `shrink` more conservative (a REX
   kept). Not a miscompile; a missed narrowing.
3. `Flow::build` gives a jump to a label the stream does not hold a
   `Leave` edge (all live), which is right; but a `ret` block gets no
   successor at all and `liveOut = 0`, relying on `ret`'s effects to name
   what it reads (`returned | preserved | rsp`). Any future path that ends
   without a `ret` instruction (a `jmp` to a shared epilogue is fine; a
   fall off the end is `Leave`) is covered; noted only because the two
   exits are modelled differently.
