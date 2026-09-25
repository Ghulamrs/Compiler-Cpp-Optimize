# Handover to session 5: the allocator landed, unjudged by the box

Fable 5.1, 2026-09-24, end of session 4 on branch
`claude/fable-5-1-background-work-kguwml` (from `gcc-scheme` at `5e6e2e3`,
session 3's head). This session ran in a Linux container: no Mac, no
Windows box, no Compiler++ tree. Everything below was measured here, and
the section at the end says exactly what the box has to run before any of
it is believed for speed or for Compiler++'s size.

Read first: `docs/OPTIMIZER-ARCHITECTURE.md` (kept in step; `mir::Allocator`
is in the diagram, the pipeline and section 7), `docs/HANDOVER-SESSION-3.md`
(the order of work this continues), `docs/GCC-SOURCE-STUDY.md` section 7
(what of IRA was taken).

## What landed, in order

| Commit | Step | What it is |
|---|---|---|
| `2f144ca` | S5a | `allocate` after `webs`: liveness and interference over the pseudos, costs collected, coloured with exactly the homes - byte-identical (2296/2296 against `5e6e2e3`) |
| `c56eecc` | S5b | The colouring: coalescing by Briggs' test and by cost, select with the copies as preferences and the home as the tie-break, the whole function back to its homes if any pseudo is left without a register |

`webs` now leaves pseudos in the stream (destroys `kPropPhysical`, keeps
`kPropFlow`); `allocate` provides `kPropPhysical`. `Function::homes` carries
the register each pseudo was found in between them. `Costs` gained
`referenceWeight(loopDepth)` (8^depth for speed, 1 for size), `Loops`' first
client in a pass.

### What S5a found: a reaching-definitions fault

Asserting the homes valid against the interference graph fired on 182
compiles. The cause was `ReachingDefs` taking an exception edge from its
block's *end*: a landing pad's read of rax - the runtime's - was joined into
the web of whatever the block defined in rax after the call, and that web
was not pinned. Harmless while every pseudo went home; wrong under any other
colouring. The edge is taken at its call now, the call's own definitions
made, so the pad's read reaches the call's pinned clobber. That is the
session-3 handover's "ReachingDefs at Edge::at (precision only)" turning out
to be correctness for the allocator.

### What S5b does today, and what it does not

Every value is still stored to the frame by the walker (S6/S7 are not
built), so the pseudos are the stack machine's short-lived ones and the gain
is copies: the moves between pseudos and into the argument registers. Over
the cases on x86_64-linux, -O1 65,077 pseudos and 4,424 copies coalesced,
-O2 66,730 and 5,585; x86_64-windows -O1 40,130 and 5,157, -O2 42,581 and
5,635. No function fell back to its homes on either target at either level.

Not built, each stated in the code and the architecture doc:

- **Spilling.** A pseudo without a register sends the whole function back
  to its homes. No function needed more; when S6/S7 raise the pressure this
  is the first thing to build (store after each def, load before each use
  into a fresh short pseudo, below `frameBase()`, then colour again).
- **Preserved registers for pseudos.** The palette is the caller-saved
  general registers; a preserved one is offered only as a pseudo's own
  home. No pseudo is live across a call today (a web in a caller-saved
  register is cut by the call's clobber), so charging a save had nothing
  to decide. With S6/S7 it does: add the save to `fn.saves` and the
  restores before the epilogue, as `promoteLocals` does, and make
  `promoteLocals` append to the saves rather than replace them.
- **A cost in bytes at -O1.** Size weighs every reference at one; the
  register order (rax..rdi before r8..r11) is assumed cheapest, not
  measured. Z1's `bytes(Instr)` would decide both.
- **The funclet rule** ("a pseudo read or written in any funclet stays where
  it is") holds by construction and not by a check: a funclet is a
  `Function` of its own with no frame group and so no pseudos, and the
  parent's pseudos reach it only through the frame, which `SharedSlots`
  already keeps.

### The coalescing rule that was not in the plan

Briggs' test alone lost on four outputs: a pseudo copied to two argument
registers was merged with one copy's partner, and then neither copy to a
physical register could go. So a merge also has to pay: the copy saved must
outweigh what the two nodes' copies to physical registers could save apart
but not together (`bestPreference`). With it, no output of the 2,296 has
more instructions than before.

## Measured here

Every number from this container; the size figures are a proxy and not the
box's.

| Check | Result |
|---|---|
| `tools/identical.sh` 5e6e2e3 vs S5a, all levels | 2296 compared, 0 differ |
| `tools/identical.sh` 5e6e2e3 vs S5b, `LEVELS=0` | 574 compared, 0 differ |
| `tools/identical.sh` 5e6e2e3 vs S5b, all levels | 1470 of 2296 differ; 0 outputs with more instructions; 81,217 lines gone, 55,244 come |
| every case at -O1 and -O2, host, run and diffed | 574 / 0 (asserts live) |
| `bash tests/run.sh` | 476 / 0 |
| `tests/emit.sh` (the -O0 golden) | 839, 0 of 839 changed |
| `tests/overload.sh`, `tests/names.sh` (clang is here) | 30 / 0, 287 / 0 |
| `tools/comment-lines --count` | 63 over the cap, the same 63 as at `5e6e2e3` |

The size proxy - every case emitted at a level, assembled by clang for its
target, `.text` summed with `llvm-size`:

| | -O1 before | -O1 after | -O2 before | -O2 after |
|---|---|---|---|---|
| x86_64-linux | 365,083 | 352,438 (-3.5%) | 436,144 | 417,997 (-4.2%) |
| x86_64-windows (GNU) | 333,856 | 322,107 (-3.5%) | 397,532 | 383,117 (-3.6%) |

Two things about this box worth knowing: `tests/run.sh` under `/bin/sh`
(dash) reports four abort-by-design cases as failures because dash writes
"Aborted" into the captured output; under `bash` it is 476/0. And
`tools/identical.sh` is not executable in the tree - run it as
`sh tools/identical.sh`.

## What the box must run to judge it

The order session 3 used, from `C:\cxxopt` (the scripts are on the box, not
in the repository; copies are in session 3's scratch):

1. `sync.cmd` with a bundle of this branch's head (`git bundle create
   cxxopt.bundle claude/fable-5-1-background-work-kguwml`), then
   `msvc\build.cmd`.
2. `opt.cmd s5 O1 O2`: Compiler++ built by the branch at both levels through
   the GNU route, its four suites, 258 cases against `C:\cxx1dev\w\cl-O2`.
   The gate is 258/258 at both levels and every suite green.
3. `cases.cmd O0 O1 O2`: cxx1's own cases at all three levels on the box
   (the Windows funclet and RTTI shapes are not run here).
4. `.text` of Compiler++ at -O1 and -O2 (GNU route; `optmasm.cmd` for the
   MASM route too). Session 3's figures to beat: 581,934 / 720,782 (GNU),
   585,598 / 723,198 (MASM). By the proxy here, expect about 3-4% less.
5. `ab.cmd 15 base-O2 s5-O2` for the bench (session 3: 734 ms, 733-736)
   and `ab.cmd 15 base-O1 s5-O1` (1078, 1076-1081). The rule from session
   2 stands: a behaviour change measured at nothing is reverted - but a
   size fall with an unchanged bench is a result at -O1.
6. On the Mac, `tools/identical.sh` with the Compiler++ tree present, to
   see the same "0 outputs with more instructions" over its units.

If the bench does not move at -O2, that is expected: the allocator has
only copies to remove until S6 and S7 give it parameters and temporaries.

## Next, in order

- **S6, S7**: temporaries and parameters as pseudos. This is what makes the
  allocator pay, and it will need the two things above: spilling, and
  preserved registers charged a save.
- The per-walk numbering of case and user labels that lets the inliner
  take callees with a `switch` or a goto label (session 3's open item).
- Z1's bytes cost, for -O1 inlining and for the register order.
