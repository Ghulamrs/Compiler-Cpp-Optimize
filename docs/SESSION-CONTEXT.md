# Session context, 2026-09-25: how to resume

The cloud session driving the optimizer rounds is running low on credit, so
this is what a new session needs to pick up.

**Repo and branches.** `Ghulamrs/Compiler-Cpp-Optimize`. The default branch is
`gcc-scheme` (there is no main/master). Work goes on
`claude/fable-5-1-background-work-kguwml` and is merged into `gcc-scheme` by PR
(PR #2 was the last merge, 6826941). The branch is ahead of it by the S11 and
S12 #1 docs and whatever S12 #2 lands.

**From 2026-09-25 on, Fable is retired at the user's instruction.** Opus does
the work directly unless the user says otherwise. The paragraph below
describes how the S5-S13 rounds ran.

**Next job: one compiler for RIDE 4.0.** Base it on C++Optimize: its -O2 runs
about 2x faster than Compiler-Cppi's (bench total 527 against 1139 ms; g++ 440).
Port Compiler-Cppi's features into it one at a time, first the tms6747 backend.
Compiler-Cppi's own optimizer is not ported. The repos share no git history, so
this is a port, not a merge. Fetch Compiler-Cppi as the remote
`cppi` (https://github.com/Ghulamrs/compiler-cppi, main 05fdbb3). The guard on
every step: x86 and arm64 output byte-identical at -O0, -O1 and -O2 to the S13
build (18d2236), and the benchmark no slower. The port had not started when this
was written.

**How a round runs.** Opus sends one step to a Fable subagent with a hard budget
(about 50 tool calls, stop by 60). The subagent prices the change by hand in
the emitted .s first and builds only if it pays at least about 5%. It commits
without pushing. Opus then checks the numbers independently and pushes.

**Gate.** Speed first, on `tools/windows/bench-kernels.cpp`: 11 interleaved
rounds, medians, checksums equal, no kernel slower than the reference, compared
with g++ -O2 and clang -O2. Correctness: `bash tests/run.sh` (480/0), every case
at -O1/-O2 (582/0), emit.sh against its golden,
`LEVELS=0 sh tools/identical.sh REF NEW` (-O0 byte-identical), `make comments`
(63 breaches or fewer), and clang `-Werror -pedantic -fsyntax-only` on touched
files, since the Mac builds with clang. The reference build is 6826941.

**x86_64-linux -O2 bench, ms (total / fib sieve matmul isort hash virtual):**
before S5 727; S8 615; S9 plus align-loops 523 (10 71 18 17 259 143);
g++ 439 (3 58 9 8 209 150); clang 384.

**Rounds since S9:**
- S10, align-entry: not landed; the gain was placement luck.
- S11, dropping hash's sign correction: 10-12% but unsound without signedness
  information; not built.
- S12 #1, hoisting the xmm load in matmul: worth 0%; not built. A hand-cleaned
  loop (13 to 5 instructions) gives 19 to 13 ms.
- S12 #2, in progress at the time of writing: the three xmm gaps priced one at
  a time (movapd copies, the repeated movslq, the addsd memory fold), building
  the best. See HANDOVER-SESSION-12.md if it landed.

**Queue after that.** S12 #2 landed; see HANDOVER-SESSION-12.md.
Skipped permanently, at the user's instruction (too long for a Fable round):
isort's redundant load and movslq (dominator-based), and fib's second level of
recursive inlining. Do not propose them again. What remains: the sound design
for S11's hash, and the Windows index shape that blocks the S12 #2 rules on
x86_64-windows.

**Only the user can run these:** the Windows box (`C:\cxxopt`,
`tools\windows\bench.cmd` against cl /O2 and cl6x at `C:\ti`) and the Mac
(`~/Documents/Claude/C++Optimize`). On the Mac, save the stale local commit
f273c6e to a branch, then reset `gcc-scheme` to origin.

**The user's rule:** work only under /Users/g.r.akhtar/Documents/Claude on
their machines, and ask permission before going beyond it.
