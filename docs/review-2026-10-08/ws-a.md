# WS-A - cpp11 truth-telling (docs and gates): handover

Branch `review/a-docs`, from main `2955c0b`, 2026-10-08. Executor Fable 5.1, finished by Claude Opus 5.5,
under `docs/REVIEW-PLAN-2026-10-08.md` section 4 "WS-A". Nothing here was pushed
or merged; the main session reads this note, re-runs the gates it wants, and
writes the CLAUDE.md section from it.

## The first hour: names.sh and overload.sh run on the Windows box

**The plan's open risk is closed in the direction it hoped.** Visual Studio's
clang is at `C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\x64\bin\clang++.exe`
(clang 19.1.5), Git's bash at `C:\Program Files\Git\bin\bash.exe`, and both
suites run there from a tree built by `msvc\build.cmd`:

    overload.sh: 31 agreed with clang, 0 differed
    names.sh:    398 passed, 6 failed

`tools/windows/names-overload.cmd <root> [compiler]` is the way to run them -
it puts the LLVM directory first on PATH, exports `CXX1` and `CLANG`, and runs
each script under bash; every other workstream's gate uses it. No shim was
needed: `clang++.exe` is in that directory under that name.

**The six names.sh failures, classified.** Two are the box's and not the
compiler's: `ifdef-comment` and `using-declaration-chain` include `<cstdio>`,
which this clang cannot compile for arm64-darwin (no Darwin headers on the box);
the Mac compares those two. **Four are real, host-independent, and nobody's
item in the plan** - they fail on any host and were not seen because `names.sh`
had not been run since the cases landed:

| case | difference | what it is |
| --- | --- | --- |
| `pipelined-data-exit`, `pipelined-reduction` | cxx1 `main.lengths` / clang `_ZZ4mainE7lengths` | the static-local naming divergence CONFORMANCE.md already records; the cases lack the `.nonames` line every other static-local case carries |
| `compare-with-zero` | clang calls `memcpy`, cxx1 does not | clang 19 emits a `memcpy` for an aggregate copy cxx1 writes inline - an emission difference wearing a naming one, the trap CLAUDE.md names three times; wants a `.nonames` |
| `ternary-index-after-remake` | clang calls `memset`, cxx1 does not | the same, for a zeroed array |

Recommended for the main session: the two static-local cases take the standard
`.nonames` line (three targets), the two `mem*` cases a `.nonames` saying clang
19 calls the C routine where cxx1 writes the loop. Not done here: `tests/cases`
is not WS-A's to edit.

**Two things about that clang an oracle user must know**, both measured:

- Its default target is `x86_64-pc-windows-msvc`, where `-fdelayed-template-parsing`
  is on and two-phase lookup is off - `tests/open/two-phase-lookup-late-name.cpp`
  is *accepted* by it without `-fno-delayed-template-parsing`. Any template-lookup
  oracle on the box must pass that flag. `overload.sh`'s clang runs with the host
  default, so an overload probe that turns on two-phase lookup would read as
  agreement there; none of the 31 does today.
- It has no C++11 library: Microsoft's STL refuses `-std=c++11` (20 errors in
  `<memory>` under `vcvars64`), and `-target x86_64-linux-gnu` or `arm64-apple-macos`
  has no headers at all. A probe that includes a header is validated on the Mac or
  not at all. `tools/mangled-names` passes `-target` and is unaffected for cases
  that include nothing.

**Where `make test` runs.** The Windows box has no `make` (not under Git bash
either), so the plan's "`make test` on the box" was done on the Linux box
(`~/ride-5.1/ws-a`, g++ 11, no clang) and the Windows box ran the pieces that
need Windows or clang: `msvc\build.cmd`, `run-cases.cmd` at -O0 and -O2,
`names-overload.cmd`, `tools/exclusions --check`, `tools/comment-lines`,
`tools/seal check`, `tools/version-lines --check`, `tools/feature-sweep`.

## Items, each with its final state

| item | disposition | state |
| --- | --- | --- |
| E1 | DOCUMENT | done - README.md "What the review of 2026-10-08 measured" carries the review's measurements, dated |
| E2, T8 | FIX headline | done - `Version.h` banner, README.md title and opening, README.1ST §3, EXCLUSIONS.md opening, CLAUDE.md first section all read "C++03 with C++11 extensions"; the way back to "C++11" is written where each says it (`tools/feature-sweep` and section 5.2) |
| E3, D4, T7, R3 (docs, Makefile) | FIX | done - EXCLUSIONS.md regenerated: **131 sites, 0 uncited, 0 stale** (was 116 / 112); `typeid`, `operator new`, placement new, `<exception>`/`<stdexcept>`, vmi and the Windows class-throw entries deleted, all implemented; `make test` fails on a non-zero `--check` (proved below); CLAUDE.md names the tool's number; README.1ST §6 and README.md's heading come from `tools/version-lines`; `tools/exclusions --check` verifies the test file every hand-written entry names |
| E8 | DOCUMENT | done - one dated sentence in README.md's "What the review measured" paragraph |
| F02 | REFUSE-BY-NAME | kept; re-cited at `ParserType.cpp:1957`; **new case `udl-refused`** (clang accepts the program); design note below |
| F05 | REFUSE-BY-NAME | kept; re-cited at `Parser.cpp:831` with `alignas-local-past-stack-refused`; design note below |
| F11 | REFUSE-BY-NAME | kept; `ParserConst.cpp:87`, `noexcept-dynamic-spec-refused` |
| F17 | REFUSE-BY-NAME + DECLINE | kept; `pending[]` is `asm export thread_local` (not the six the old document listed), `refused.cpp`; the four TLS models are in EXCLUSIONS.md's keyword section |
| F28 | REFUSE-BY-NAME | kept; `ParserTemplate.cpp:63`, `template-variadic-nontype`; design note below |
| F32 | REFUSE-BY-NAME | kept; `ParserTemplate.cpp:43`; **new case `template-template-parameter-refused`** (clang accepts) |
| F35 | DECLINE + test | done - `tests/open/two-phase-lookup-late-name.cpp` (`.clang` REFUSE; cpp11 prints 7); CONFORMANCE.md gained the entry it never had ("A name declared after a template is found inside it"); design note below |
| F36 | REFUSE-BY-NAME | kept; `ParserType.cpp:986`, `ref-qualifier-refused` |
| F37 | REFUSE-BY-NAME | kept; `ParserType.cpp:441`, the `.notarget` lines carry the reason |
| F40 | REFUSE-BY-NAME | kept; `ParserStmt.cpp:2110` and `ParserTopLevel.cpp:1235` with their two `.error` cases |
| F42, F43 | DOCUMENT | done - README.md "What is absent" and "Known shortcomings in 1.6" no longer say `typeid` or `operator new` are refused; EXCLUSIONS.md's RTTI and new sections say what is left |
| D7 | DOCUMENT | done - README.md's suite paragraph: the golden is a before/after read by hand, never fails, is not an oracle; four suites stay four |
| D8 (docs half) | FIX | done - `tools/version-lines` writes README.1ST §6 (version, date, seal file, banner quotes, the cross-reference) and README.md's heading, run by `tools/seal write`, checked by `tools/seal check`; README.1ST §1's option list mirrors `--help` (masm / gnu / ml64, -O levels, -version); `-g` is said to be **three** targets, not four: `Tms6747::emitsLineTable` is false and the driver refuses `-g` there - the plan's "all four" was wrong. `-version`'s "PST" is `Driver.cpp`, WS-F's |
| D11 | DOCUMENT | verified: `find tests/cases -name '*.nocl' -size 0` finds 0 of 110 at 2955c0b |
| V2 | DOCUMENT | done - README.md splits 402 recorded outputs from 226 recorded refusals (628 cases); `run.sh` prints `(N printed their recorded output, M were refused as recorded)` - the one script edit, nobody else owns `tests/run.sh` |
| V4 (doc) | DECLINE + DOCUMENT | done - README.md names the optimizer's gates; the `Mir` verifier design note is below |
| every REFUSE-BY-NAME item | re-citation | every one of the 131 sites is cited at its 2026-10-08 line |
| section 5.2's `tools/feature-sweep` | write | done - 94 probes from the review's matrix; see below |

## What was measured, and where

**Gates on the Linux box** (`~/ride-5.1/ws-a`, g++ 11, `./build` under the
300 MB cap), `make test` at `dd32ee2`:

    run.sh: 627 passed, 0 failed (400 printed their recorded output, 226 were refused as recorded)
    emit.sh: 1582 passed, 0 failed    (no golden on that box - a golden is local)
    names.sh / overload.sh: skipped - no clang++ there, as always
    131 sites, 0 uncited, 0 stale citations (18 hand-written entries name a test file, 0 missing)
    comment-lines: 0 group(s) over the cap

(627 = 400 + 226 + the quoted-pattern check; two cases skip on that host by `.notarget`.)

**The Makefile gate, proved as the plan asks.** A line
`// src_.fail(pos, "a gate probe is not supported yet");` appended to
`src/Operator.cpp` (the tool reads text, so a comment is a refusal to it) and
`make test` run: exit 2, `not in the document: src/Operator.cpp:79  a gate probe
is not supported yet`, `132 sites, 1 uncited`, `make test: docs/EXCLUSIONS.md
does not match the source`. The line was removed and the tree is clean.

**Gates on the Windows box** (`C:\cxx1\rtsdiv\ws-a`, `core.autocrlf=false`,
`msvc\build.cmd`): `tools/exclusions --check` 131 / 0 / 0; `comment-lines` 0;
`version-lines --check` agrees; `tools/seal check` - **1 file differs,
`src/Version.h`**, which is the banner change and is expected: the reseal is
the main session's. `cxx1-msvc.exe -version` prints the new banner. The
Windows cases (`run-cases.cmd`, GNU spelling, compared as `tools/verify-three`
does), built at `41b10cb` for -O0 and at `dd32ee2` for -O2 - the same `src/`:

    -O0  379 printed their recorded output, 0 failed; 226 refusals as recorded, 0 failed
    -O2  379 printed their recorded output, 0 failed; 226 refusals as recorded, 0 failed

At `41b10cb` on the Linux box: `exclusions --check` 131 / 0 / 0, `comment-lines`
0, `version-lines --check` agrees, `ide/generate.py --check` clean. On the Mac,
`tools/seal check`: 155 files, 1 differs, `src/Version.h`, as above.

**The emit golden did not move.** This workstream changed no code generator and
no parser rule; `Version.h`'s banner is printed to stderr and never emitted. The
Linux `emit.sh` passed 1582 / 0 with no golden to diff against (goldens are
local and the box had none); the main session's own golden is the oracle for the
claim, and the claim is that nothing emitted changed.

**`tools/exclusions` found one defect of its own on the box**: it spelled paths
with `\` on Windows and so read every citation as stale (131 / 131). Fixed
(`1e63400`); 0 / 0 on both hosts now.

## `tools/feature-sweep`, and what it found

Section 5.2 asks WS-A to write the review's 122-probe grid as a tool for the
release step. `tools/feature-sweep` holds **94 probes**, one per row of the
review's section 2 (its 122 included several per row), each a complete program
that is valid C++11 or is marked as a form the standard refuses. `--clang`
validates each against clang first and prints the disagreements as the sweep's
own faults rather than the compiler's. Run on the Windows box at `48e8769`
(`CXX1=./cxx1-msvc.exe CLANGFLAGS=-fno-delayed-template-parsing`):

    compiles                 38
    refused by name          50
    refused naming nothing    6

**The six in the invisible bucket, each another workstream's:**

| probe | cpp11 says | whose |
| --- | --- | --- |
| `ucn` - `int caf\u00e9` | `stray '\' in program` | D (lexer): a UCN in an identifier |
| `forwarding-reference` - `f(T &&)` given an lvalue | `no function called 'f' takes these 1 argument(s)` | E1 (F23) |
| `initializer-list-ctor` - `sum({1, 2, 3})` | `expected an expression` | E2: a braced call argument |
| `nullptr-t` | `'std::nullptr_t' was not declared in 'std'` | L: `<cstddef>` |
| `default-function-template-arg` | `'T' appears in no parameter` | E1 (F31): a default template argument on a function template |
| `lib-to-string-stoi` | `'std::stoi' was not declared` | L (L07) |

The ten "oracle faults" it prints on the box are every probe that includes a
header - the box's clang has no C++11 library to compile them against - and
`two-phase-lookup` compiles where the standard refuses it, which is the F35
decision. The headline decision at the release step reads this tool's three
counts; today they say what the review said.

## Design notes for the kept refusals

- **F02, user-defined literals.** [lex.ext]: the lexer has to carry the suffix
  with the literal, the parser look up `operator"" _suffix` by the literal's
  kind - integer, floating, character, string - and the raw form
  (`const char *`) and the literal operator *template* (`template <char...>`)
  need a non-type parameter pack, which is F28. Four lookups and a pack: not a
  48-hour item, and nothing in the product writes one.
- **F05, `alignas` on a local past the stack's alignment.** The stack is kept
  16-byte aligned on the three hosts and 8 on the C6000; a local asking for more
  needs the frame realigned at entry - `and rsp, -N` after the prologue, a
  second base register for the incoming arguments, and the unwind codes on
  Windows and the C6000 rewritten for a frame whose size is not a constant.
  Four targets, four unwind formats. A global or a member may ask for more
  today.
- **F17, `thread_local`.** Four models, none shared: ELF's TLS relocations and
  `__tls_get_addr` (or the local-exec `%fs:` form), Mach-O's `__thread_vars`
  and `_tlv_bootstrap`, COFF's TLS directory with `_tls_index` and
  `__tls_array`, and nothing on the C6000, which has no threads in its
  runtime. A keyword implemented on three targets and refused on the fourth is
  the half-built thing this tree refuses everywhere; so it stays in `pending[]`
  and is refused by name at all three doors.
- **F28, non-type parameter packs.** A pack of types is bound to a *list of
  types* and expanded by lookup of the names its members were given; a pack of
  values needs a second list of values beside it, a second expansion, and the
  Itanium spelling `J...E` of a value list in every name.
- **F32, template template parameters.** A third kind of template parameter
  binding - a template rather than a type or a value - with the pattern read
  spelling it `TT_` on Itanium and the substituted signature on Microsoft,
  and `C<int>` inside the body meaning "instantiate whatever C is bound to".
- **F35, two-phase lookup.** A conforming answer needs each template body
  parsed once into a tree carrying *dependent* on every name, non-dependent
  names bound at the definition and only dependent ones at the instantiation,
  and ADL at the instantiation for dependent calls. That is a second parser
  pass and a dependent AST this compiler was deliberately designed without
  (CLAUDE.md, rung 5); the over-acceptance is counted in `tests/open` now
  rather than only described.
- **F36, ref-qualifiers.** The implicit object parameter would have to carry
  the object's value category into ranking, and both manglers spell the
  qualifier (`R`/`O` on Itanium after the cv, `G`/`H` on Microsoft).
- **V4, a `Mir` verifier.** What a consistency check would assert after every
  pass: every pseudo read has a definition that reaches it, every label a jump
  names exists once, every call's clobber set is honoured by the allocator's
  live ranges, and the flow graph the passes read matches the instruction
  stream (the 2026-10-07 divide-by-constant fault was a pass reading a stale
  flow). Run under an environment variable the way `CPP11_C6XLIVECHECK` is,
  on in the suites. Not written this round; the gates that stand in for it are
  named in README.md.

## What is left, and for whom

- The four real `names.sh` failures above want `.nonames` lines (whoever owns
  the cases; not this workstream).
- `Driver.cpp`'s `--help` says `-g` is "x86_64-linux and arm64-darwin only",
  and the `-g` refusal text speaks of MASM even on tms6747; `-version` prints a
  hard-coded "PST". All three are `Driver.cpp`, WS-F (D8's other half).
- `examples/Makefile:3` and `examples/build-win.cmd:4` quote the old banner in
  a comment; not WS-A's files, harmless, and `version-lines` could learn them.
- The CLAUDE.md section for this round is the main session's to write from this
  note; nothing in CLAUDE.md beyond lines 1-60 was touched.
- The seal: `src/Version.h` changed (the banner), so `cxx1-1.6.dat` differs by
  one file until the main session reseals (`tools/seal write` now rewrites the
  README version lines first).
