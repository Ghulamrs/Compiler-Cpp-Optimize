# PROBLEM C++ - cpp11 drops a member template that uses a templated constructor

Written 02-10-2026 evening, to be handed to Fable 5.1 as a fresh job. Self-contained: everything
needed is below.

## The use case: SampleExt

The user's CCS sample project **SampleExt** (`~/Documents/RIDE/ccs/SampleExt` on the Mac - do not
edit it; a copy of its four sources is in `~/Developer/Claude/build/sampleext-usecase/`): `Math.cpp`,
`Vector.h`, `Matrix.h`, `Quat.h`. Templates over `size_t`: `CVector<N=3>`, `CMatrix<M=3,N=3>`,
`CQuat<I=4> : CVector<I>`, `CRotrix<M=3> : CMatrix<M,M>`. `main` multiplies matrices and vectors and
prints with `std::cout`.

Built in RIDE 4.51 on the Mac (arm64-darwin) it fails at the link:

    Undefined symbols for architecture arm64:
      "CVector<3ul> CVector<3ul>::operator*<3ul>(CVector<3ul> const&) const", referenced from: _main

Same with the newest cpp11 (Version 1.4, `f8f0e38`, built 02-10-2026) on the Linux box for
x86_64-linux (`undefined reference to CVector<3ul>::operator*<3ul>...`), and in RIDE 4.7 as a CCS
project for tms6747 on vm6747 (`undefined symbol '_ZNK7CVectorILj3EEmlILj3EEES0_RKS0_'`). Every
target: a front-end fault.

## The program is correct - measured

- **g++ 11.5** (Linux box), `-std=c++11 -Wall -Wextra -pedantic`, `-O0` and `-O2`: no warning, runs,
  both print the same.
- **clang++ 19.1.5** (Windows box, VS's LLVM, MSVC STL so `-std=c++14` - MSVC's STL cannot be C++11):
  no warning, runs, output identical to g++ line for line.
- **TI cl6x 7.4.4 (CCS 5.5)** on the Linux box: builds (one warning, `Vector.h` line 45 "subscript out
  of range" - the dead `if (N > 3) vector[3] = v4;` in `CVector<3>`), runs to exit on TI's C6747
  cycle-accurate simulator, 167,479 cycles (prints nothing: TI's `<iostream>` writes no console on the
  simulator). CCS 7.4 (cl6x 8.2.2) is on the Windows box only and was not tried.

Reference output (g++ = clang++), what cpp11 must print:

    Hello Math!
     1 0 0 0
     0 0 0
     0 0 0
     0 0 0
     0 0 0

    A =
     1 2 3
     4 5 6
     7 8 10

    B =
     2 0 1
     1 3 2
     0 1 4

    C = A * B =
     4 9 17
     13 21 38
     22 34 63

    a = 1 2 3
    b = 4 5 6
    a * b = -3 6 -3
    a . b = 32
    a ^ b = 4 10 18
    A * a = 14 32 53
    Rz(0.5) =
     0.877583 0.479426 0
     -0.479426 0.877583 0
     0 0 1

    Rz * a = 1.83643 1.27574 3

Not errors, noted for the user: the dead out-of-range store above; constructors spelled
`CVector<N>(...)` (valid C++11/14, ill-formed from C++20 - CWG 2237); `CRotrix::aboutZ` builds the
transpose of the usual rotation (a passive convention - the code's choice).

## What is wrong in cpp11 - narrowed on the Linux box

The failing member, `Vector.h` around line 126:

```cpp
template<size_t K>
CVector<K> operator*(const CVector<K> &rhs) const {
    CVector<N> lhs(*this);
    assert(N == 3 && N == K);
    return (CMatrix<K, K>)lhs * rhs;
}
```

Replacing the body line, one variant at a time (same headers otherwise):

| body | result |
| --- | --- |
| `return rhs;` | links and runs |
| `CMatrix<K, K> m; return m * rhs;` | links and runs |
| `CMatrix<K, K> m(lhs); return rhs;` | **undefined reference** |
| `CMatrix<K, K> m = (CMatrix<K, K>)lhs; return rhs;` | **undefined reference** |
| `return CMatrix<K, K>(lhs) * rhs;` | **undefined reference** |
| the original line | **undefined reference** |

A small `V<N>` with a member `operator*<K>` and no templated constructor works in all three spellings
(`a * b`, `a.operator*(b)`, `a.mul(b)`), so member-template operators themselves are fine.

The construct that breaks it is `Matrix.h`'s **constructor template**:

```cpp
template <size_t K>
CMatrix<M, N>(const CVector<K>& v) { ... }
```

Written outside any template, cpp11 says so - the candidate list leaves the template constructor out:

```cpp
#include "Matrix.h"
int main() { CVector<> a(1, 2, 3); CMatrix<> m(a); std::cout << m; return 0; }
```

    t3.cpp:2:46: error: no function called 'CMatrix<3,3>::CMatrix<3,3>' takes these 1 argument(s)
        candidate: CMatrix<3,3>()
        candidate: CMatrix<3,3>(const double *, const double *, const double *)
        candidate: CMatrix<3,3>(class CVector<3>, class CVector<3>, class CVector<3>)

## The two defects

1. **A constructor that is itself a template is not a candidate** (direct-init `CMatrix<> m(a)`,
   functional cast `CMatrix<K,K>(lhs)`, C-style cast `(CMatrix<K,K>)lhs`, copy-init). It has to be
   declared, deduced (K from `CVector<K>`), ranked with the other constructors, instantiated and
   emitted - on all four targets, with names agreeing with clang (`tests/names.sh`) and cl.
2. **A failure inside an instantiated member template's body is swallowed**: the function is dropped
   and only the linker (or vm6747) notices. It must be reported as the error it is, with the place
   that asked for the instantiation. Find out whether this is the SFINAE `Trial` catching a hard error
   (CLAUDE.md: "A refusal is not a substitution failure") or the deferred member-body replay
   discarding errors.

## Acceptance

- SampleExt (the four sources) compiles with cpp11 and prints the reference output above on the Linux
  box (x86_64-linux), on the Windows box (x86_64-windows), and as tms6747 on vm6747; and on TI's C6747
  simulator through ASM6x + LNK6x/lnk6x a `printf` variant prints the same values as cl6x's build.
- New cases in `tests/cases/` (clang's output as `.expected`): a constructor template by direct-init,
  functional cast, C-style cast and copy-init; deduced from a class-template argument; ranked against
  ordinary constructors (and an ambiguity clang refuses, as a `.error`); used inside a member template
  of another class template (the SampleExt shape); and a case where an error inside an instantiated
  member body is reported rather than dropped.
- Gates: `tests/run.sh`, `tests/emit.sh` (golden: say how many files changed and why),
  `tests/names.sh`, `tests/overload.sh`, `tests/tms6747.sh` at -O0/-O1/-O2; `tools/exclusions --check`
  and `make comments` (comments at most three lines). Three boxes per CLAUDE.md.

## Rules for the job

- Work in the worktree **`~/Developer/Claude/C++Optimize-wt-tmplctor`**, branch **`tmpl-ctor`**, made
  from `gcc-scheme` at `f8f0e38`. Read `CLAUDE.md` there first (the reading order it gives).
- **The Mac is a control room**: build and run the suites on the Linux box (`ssh ec2-user@52.202.164.123`)
  and the Windows box (`ssh windows`) - `tools/verify-three` relays the working tree. Not on the Mac.
- TI's simulator: one run at a time on the Linux box; at most 30 minutes per run on the Windows box.
- **Do not push, merge or reseal.** Commit on `tmpl-ctor` and report: what
  was wrong, what changed, every gate's numbers, SampleExt's output on each target.
- The user's files (`~/Documents/RIDE`, `~/.ride`) are not to be touched.

## State at the time of writing

- RIDE 4.7 (`github.com/Ghulamrs/RIDE-4.7`, `776fd90`): its Mac installer is built
  (`~/Developer/Claude/RIDE-4.7/dist/RIDE-4.7-macos.pkg`), not installed - the user runs the four sudo
  commands (remove 4.51, forget its receipt, install 4.7). Its cpp11 is `f8f0e38`, so it has this fault.
- The Linux box holds the reproduction: `~/sampleext-run/` (SampleExt as a CCS project, `run.sh` for
  RIDE 4.7 + CCS 5.5 + simulator), `~/sampleext/` (the narrowing variants), `~/mt/op.cpp` (the small
  case that works).
- The Windows box: `C:\Users\GRA\sampleext\` with `clang.cmd` (clang++ 19 runs).

## Resolved

Fixed in 50c9759 (Fable 5.1, branch tmpl-ctor), merged into gcc-scheme on 02-10-2026: constructor templates are candidates, an error in an instantiated member-template body is reported, and a cast to a class reaches its converting constructors. SampleExt prints the reference output on x86_64-linux, x86_64-windows and tms6747; see CLAUDE.md, "A constructor that is a template, and the body error a trial was swallowing, 2026-10-02".
