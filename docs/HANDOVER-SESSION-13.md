# Handover to session 14: three Windows miscompiles, and cxx1 against cl and g++

Opus 5.5 with a Fable 5.1 agent, 2026-09-25, from `d90fd19` to `f761eb4`
(PR #6). The Mac, the Windows box and the EC2 Linux box, all three used.
Read `docs/SESSION-CONTEXT.md` for how rounds run and
`docs/HANDOVER-SESSION-12.md` for S12 #2.

## What happened

The Mac suites were green on `d90fd19`. The Windows box, running cxx1's
cases and Compiler++ at -O1 and -O2 for the first time since the S5 work,
was not: 36 cases failed at -O1 and 30 at -O2, and Compiler++ built by
cxx1 crashed on every input at both levels. Rounds S6 to S12 all ran in a
Linux container, and all three faults below are invisible on
x86_64-linux.

| | Symptom | From | Mended in |
|---|---|---|---|
| 1 | A `double` passed to `printf` printed `0.0` (28 cases) | `9d937e2` | `cd7ab59`, `OptValues.cpp` |
| 2 | A member function with no parameters returning a class by value wrote through garbage; Compiler++ crashed | `1f53a77` | `6f5eef5`, `X86_64Linux.cpp` |
| 3 | -O2 only: every Compiler++ diagnostic had a garbage line and column | `11d3e3d` | `278b339`, `Optimizer.cpp` |

1. **The Windows varargs double.** The convention wants it in the xmm
   register and the matching integer register, so it is stored to a
   temporary and loaded twice. `pairTemp` could not pair the integer load
   and left the entry in `temps_`; the xmm load then paired and killed the
   store the first still read. The entry now goes whenever a load of the
   slot stays. Bisected by reading the emitted assembly on the Mac.
2. **The return pointer's slot.** Microsoft puts `this` before the hidden
   pointer, and `placeArguments` counted the pointer's slot only on
   reaching the second argument. With `this` alone the call's plan named
   one register, and since `1f53a77` the optimizer believes the plan, so
   `lea slot, %rdx` was dead and went. Bisected by running on the box.
3. **Overlaid inlined frames.** Two callees walked in place at one base
   share bytes. In Compiler++'s `Parser::parseAddSub`, `advance()`'s
   64-byte `Token` temporary lies over `BinaryExpr`'s constructor
   parameters; `11d3e3d` offered those parameters to the allocator, and
   the `Token`'s word-by-word copy reads exactly like a scalar's. Each
   callee's non-scalar bytes are now kept for the function, and no scalar
   over them is offered. Found and mended by a Fable 5.1 agent, then
   rebuilt and re-run independently. `tests/cases/inline-overlaid-frames`
   reproduces it; -O2 `.text` of Compiler++ +0.77%, -O1 unchanged.

Measured at `f761eb4`: Mac run 484/0, emit 859/0, names 295/0,
overload 30/0, comment-lines 63. Windows box (cl build): cases 271/271 at
each of -O0, -O1, -O2; Compiler++ at -O1 and -O2 129/0, 67/0, 90/0, 9/0
and 258 of 258 identical to cl. EC2 (g++ build, `-Werror`): run.sh
484/0, every case at -O1 and -O2 294/0.

## cxx1 against cl and g++, on Compiler++

The same Compiler++ source (hashes checked on all three machines) and the
same `bench.cpp`, built at -O1 and -O2 by the platform compiler and by
cxx1. Every one of the eight builds passes the four suites and matches on
all 258 programs; the workloads' output hashes are equal across builds and
across platforms. 13 interleaved rounds, medians; no CPU steal on the
t3.nano. cxx1 divided by the native compiler at the same level:

| | Win -O1 | Win -O2 | Linux -O1 | Linux -O2 |
|---|---|---|---|---|
| build time | 0.18 | 0.21 | 0.82 | 0.89 |
| `.text` | 1.07 | 1.11 | 1.65 | 1.90 |
| tests workload | 1.12 | 1.11 | 2.88 | 2.77 |
| `bench.cpp` | 2.60 | 1.55 | 2.71 | 1.66 |

What it says: cxx1 compiles faster everywhere; its -O2 code runs at about
1.6 times the native compiler's time on both platforms; its code is close
to cl's size and nearly twice g++'s. Windows' process start-up hides the
tests workload's gap, which Linux shows. The page is
`https://claude.ai/artifact/1VeHW2J5TNdFXQFnXJ3dxG` (private), built by
`~/cxxopt-build/compare/make_page.py` from `compare/*.csv` (Windows) and
`compare-linux/*.csv` (EC2); the EC2 scripts are `~/cmp/{build,check,bench}.sh`.

## The gate, changed

**A round is not done until the Windows box has run it.** The container's
x86_64-linux runs cannot see the Microsoft ABI's varargs, argument order
or `this`-first return pointer, and three miscompiles lived through six
rounds that way. Before landing: `C:\cxxopt\cases.cmd O0 O1 O2` (271 each)
and `C:\cxxopt\opt.cmd TAG O1 O2` (Compiler++ against cl). A cloud session
with no box says so and leaves the round for one that has it.

## Tools this session left

- **`C:\cxxopt\bis\srun.cmd NAME`**: assembles, links and runs one GNU-syntax
  `.s` for x86_64-windows, in seconds. Emit it on the Mac with
  `-arch x86_64-windows -S`.
- **`C:\cxxopt\bis\cpprun.cmd`**: the same for Compiler++'s 16 units, then
  runs it on `11_err_recovery.cpp`.
- **`~/cxxopt-build/probe-cpp.sh DIR`**: builds cxx1 in DIR, emits
  Compiler++ at -O2, runs it on the box.
- **`~/cxxopt-build/bisect-run.sh`, `bisect-cpp.sh`**: `git bisect run`
  scripts in the same shape, the second laying the earlier fixes on each
  commit (`fix-call.patch`, `fix-xmm.patch`, `fix-xmm-old.patch`) and
  retrying a `-Werror` failure without it.
- **Mixing units and functions**: with a known-good and a faulty compiler,
  splicing whole units and then single functions between their assembly
  found fault 3 in five box rounds.

## Not done

- The MASM spelling at -O1 and -O2 on the box (ml64 route), and
  `tools/verify-three` (the -O0 suites against cl): not run this session.
- S12 #2's rules still do not fire on x86_64-windows: the index there is
  `movslq; shl $3; movslq`, which `fold-index` leaves.
- S11's hash sign correction still wants a soundness argument.
- The code-quality gap above is the subject if the optimizer continues:
  -O2 `.text` 1.9 times g++'s on Linux, and 1.6 times the run time.
