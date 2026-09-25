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
build (18d2236), and the benchmark no slower. Step 1, the tms6747 backend,
is done (8743547, ba50daf, 982cb5a): tests/tms6747.sh runs 280/0 on the VM6747
emulator (clone github.com/Ghulamrs/VM6747, build Emulator/, pass
VM=.../vm6747.exe). Step 2 is the features Compiler-Cppi has: its 58 test
cases that this tree lacks, run here, fail 50. docs/PORT-FROM-CPPI.txt lists
them, grouped by why each fails; work through it, one feature per commit,
with the same guard. The reference for x86 identity is a build of 18d2236.

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

## State at the end of the cloud session, 2026-09-25

The cloud session was closed because it cannot reach the Mac, the Windows box
or the Linux box. Everything it did is on the branch
`claude/fable-5-1-background-work-kguwml`. It is **not yet merged** into
`gcc-scheme`: S13 and the whole port are waiting for a run on the boxes.

**Done on the branch, in order:**
- S13 (18d2236): the Windows index scaled at pointer width.
- tms6747 port, step 1 (8743547, ba50daf, 982cb5a): the backend is in.
  tests/tms6747.sh runs 280/0 on the VM6747 emulator. To run it, clone
  github.com/Ghulamrs/VM6747, `make` in Emulator/, and pass VM=.../vm6747.exe.
- Compiler-Cppi features, one cherry-pick each (`-x` names the origin):
  enum-base/alignas (c2a4255), pointer throw/catch (4bbc1b6), operator new and
  delete (28bf9bb, plus Compiler-Cppi's lib/ C headers and include/new), typeid
  (d8dd41f), arrays of a class with a destructor (d7203ae), wchar_t (b26cf25),
  virtual inheritance proper (149f624), the cl-review rows bd03342 and 64d7f16,
  51ab91b, and 6a725f4.
- tools/windows/bench-own.cmd: cxx1 through the project's own MASM and LINK, compared with
  ml64 + link.exe and with cl, at -O1 and -O2. Not run yet. MASM was checked
  here: it assembles cxx1's -O1 and -O2 output of the benchmark.

**The guard held at every step:** tools/port-gate.sh. The x86 output of every
existing case is byte-identical to the S13 build, except where a step was meant
to change it. Those changes are named in each commit: virtual-inheritance
layout and thunks (17 cases), weak template statics, the A21 lambda reuse
(nested closure named `$deduced_0` where clang writes `$_0`), and one register
choice. The benchmark's -O2 assembly is byte-identical to S13's throughout, so
no speed was lost.
Last numbers: run.sh 521/0, names 324/0, overload 30/0, tms6747 313/0,
comments 63.

**Next, in this order.** Compiler-Cppi commits still to port:
1. 87abbb2 - the noexcept table; one comment conflict in src/Ast.h, take Cppi's.
2. e7ea77d - a base pointer adjusted from a derived object's address.
3. 8d506e9 - A19, a qualification conversion into an array element.
Then re-run Compiler-Cppi's own tests/cases against this tree (docs/PORT-FROM-CPPI.txt
has the method). What was still failing at the close: noexcept-local-terminates,
static-base-pointer, qualification-into-array (the three commits above),
operator-delete-virtual (wrong output), runtime-shapes (needs <cstdint>), and
three refusal cases that are accepted here (enum-base-range-refused,
operator-new-static-refused, typeid-copy-refused).

**Held back at the user's instruction:** the pointer to a virtual member function
(Compiler-Cppi f31fd96). It needs every function at an even address. Compiler-Cppi does that
with `.p2align 4` on every function, which slowed the virtual kernel from 142 to
183 ms here. `.p2align 1` would be enough for correctness and was never measured.

**Not decided:** merging into gcc-scheme; renaming the binary cxx1 -> cpp11 as
Compiler-Cppi did; an optimizer for tms6747 (it runs unoptimized today).
