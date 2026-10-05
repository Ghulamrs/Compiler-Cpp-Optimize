# Handover: the C6000 size level, closed, 2026-10-05

The branch `c6x-matmul-sieve` of 2026-10-02 (head 0520f9e) finished and measured. Nothing pushed, no
branch deleted, the main checkout untouched. Worked in `.claude/worktrees/agent-aabb1b1146ee29986`.

## The commits on `c6x-size-final` (the merge at its head)

e14e35d frame slots / $fill / exidx (was WIP 03325fd), 1feb437 a freed slot belongs to its frame
(469d0e2), e2947d7 guardsJump's comment (6135bcd), c17f5cc merge tms-opt, 89721e5 the corpus
triaged, b1bb23d CLAUDE.md of 2026-10-01, 4fa3e6c -Os (b2062c1), 46cbb00 the 10-02 handover,
15561ad foldScaledIndex's reader + the -Os column, 9dc38f7 the packeted label branch's side,
417307b this round's docs; then the merge of all of it onto 0407df9.

## The branches

- **`c6x-size-work`**: 0520f9e's nine commits with the four WIP messages reworded by `git
  filter-branch --msg-filter` (trees byte-identical to 0520f9e, checked by `git diff`), then the two
  fixes and this round's docs on top.
- **`c6x-size-final`**: gcc-scheme 0407df9 plus `c6x-size-work`, ending in the merge commit. This is
  the branch to verify and fast-forward gcc-scheme to. One conflict in the merge, `tests/corpus.sh`,
  resolved by taking gcc-scheme's (dcf6565: the .cpp copy with the headers beside it and the sharper
  FAILING line; the branch's own copy did the same with `-Itests/c-corpus`).
- `c6x-matmul-sieve` is left as it was, at 0520f9e, local and on GitHub.

## The two defects the gates found (commits on `c6x-size-work`)

1. `foldScaledIndex` ended its scan at an instruction that reads T and writes it (`ADD A16, A6, A6`,
   a store's address from the same scaled index), removed the SHL, and left that ADD reading a
   register nothing set: eight emulator failures at -O1/-Os, two at -O2. Such an instruction is an
   unfolded reader now.
2. ASM6x refused sixteen -O1/-Os outputs: `STW || MVKH g, B12 || MV B4, A10 || [!A1] B end` - it
   puts the crossed MV on .S1, moves one instruction at most, and the unnamed B finds both .S taken.
   `foldBranchNops` now names a packeted label branch's side (`B .S2 label` / `.S1`) whether or not
   a NOP follows. The kernels' text differs by the unit name alone; their cycles do not move.

## The gates, on the final tree

| gate | result |
| --- | --- |
| run.sh (Mac) | 580 / 0 |
| names.sh | 369 / 0 |
| overload.sh | 31 / 0 |
| make comments | 0 over the cap |
| tms6747.sh -O0 / -O1 / -O2 / -Os (vm6747) | 356 / 0 at each |
| ASM6x on every -O1 / -O2 / -Os output | 356 / 356 clean at each |
| emit golden, recorded at 0407df9 | 457 of 1443 changed: 147 hosts (frame offsets, sizes and the unwind words that encode them - nothing else, checked by normalising those), 310 tms6747 (offsets, the short frame form under 124, exidx entries of non-callers leaving) |
| verify-three (Linux and Windows boxes) | **not run** - the user closed the job at 15 minutes' notice before it could start on the final tree; a Linux leg begun on an earlier tree was stopped and nothing of it is claimed. The Windows box was reached for every simulator run, so both boxes are up for the main session's own run. |

## TI's C6747 simulator (`tools/c6747-levels`, now with an -Os column)

Run `1005-173724` on the Windows box (six hash sessions lost on the first pass - the box's
known lost-session fault - re-run with `STAMP=1005-173724 RESUME=1`, 72 of 72 as expected).
The tables are in CLAUDE.md, "The C6000 size level landed". Headline: -O2 speed 0.76x-1.20x
CCS 7.4 -O2 on every kernel; -Os size 1.25x-1.85x CCS 7.4 `-O2 -ms3`, 1.54x in total - **the
1.3x size goal is not met on the kernels**, and is met on the Compiler++ harness (520,192 bytes
at -O1 = 0.94x CCS 7.4 -O1, 1.08x its `-ms3`; -Os 519,680; -O2 584,288 = 0.85x). LNK6x's images
take the TI-linked images' cycles on every kernel at every level. `tools/c6747/ctor` (run
`1005-174331`, 12 of 12 as expected): cycles 744-O2 209,167, 822-O2 205,812, 822 -ms3 207,354,
cpp11 -O1 210,160, -O2 209,981, -Os 210,160 (1.02x 822-O2, 1.01x 822 -ms3); code bytes 744-O2
864, 744 -ms3 800, 822-O2 896, 822 -ms3 800, cpp11 -O1 960, -O2 1,184, -Os 960 (1.20x -ms3).

The harness cycles at -O1 were **not** re-measured on the simulator (ten box runs; the -O1 object
is 1.4% smaller than at 0407df9 and the change is frame slots and exidx, which the kernels show
as cycle-neutral); the input benchmark at -O2 of 2026-10-01 stands as the last cycle figure.

## Not done, and why

- Step 3 of the 2026-10-02 handover (a called-once static inlined at -Os, `ZERO` for 0.0, B3 in
  B9 round a leaf's helper call): none is small, and the user's instruction was to take size work
  only if small and safe. The kernels stay at 1.54x `-ms3` in total.
- `tools/c6747-levels` adds `-Os` for cpp11 only; cl6x's `-ms3` column is what it is measured
  against, as the tool's ratio table says.

## Scratch

- Mac: `/private/tmp/claude-501/.../scratchpad/` - `base/` (a worktree of 0407df9 used to record
  the golden and for before/after harness bytes), `harness/`, `kern/`, `asm2/`, the gate logs.
- Windows: `C:\cxx1\bench-levels\1005-173724` (kernels), the ctor run's folder beside it,
  `C:\cxx1\fable-c6x-size\` (cl6x objects, from 2026-10-02).
- Linux box: nothing left running (one orphaned build from an aborted verify leg was killed).
