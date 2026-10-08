# WS-F - the Windows MASM path, the driver, parallel safety: handover

Branch `review/f-windows` from main `cc210bb` (WS-A merged), 2026-10-08, executor Claude Opus 5.5,
under `docs/REVIEW-PLAN-2026-10-08.md` section 4 "WS-F". Two more branches of the same name, both
local and unpushed: **MASM** `review/f-windows` (tests only, `66baab1`) and **LINK**
`review/f-windows` (**two `src/` fixes** and its bed, `e8d51b5`, `c5548c7`) - the plan expected
tests only there; a real fault was found in LINK and is said below. Nothing was pushed or merged.

Builds and tests ran on the Windows box (`C:\cxx1\rtsdiv\ws-f`, `ws-f-masm`, `ws-f-link`,
`core.autocrlf=false`, `msvc\build.cmd`; the project's masm and LINK built there by cl from
their trees) and on the Linux box (`~/ride-5.1/ws-f`, g++ 11.5). The Mac edited and ran git.

## Items, each with its final state

| item | state |
| --- | --- |
| E5 / D15 / R2 / V5 | **fixed**. The 10 refusals reproduced exactly (369 of 379 assembled by the project's masm). Two faults in `Masm.cpp`, two in LINK (below). `run-cases.cmd` runs both spellings by default; verify-three ships MASM and LINK and compares both at -O0 and -O2 |
| `.data SEGMENT ... COMDAT` | **fixed** `4af116e`: a COMDAT data block is `_DATA`, `_BSS` or `CONST`, the classic names the project's masm maps to `.data`, `.bss`, `.rdata`. Every object that assembled before is **byte-identical** after (369 of 369) |
| `$LNleave$...$catch$N` duplicate | **fixed** `4f55d38`: not two definitions - an early exit's `jmp $LNleave$` made the label an `EXTERN` (the `referenced_` set), and the funclet's raw-text definition then collided under `OPTION NOSCOPED`. The funclet's labels are now `predefine`d |
| ml64 dialect, every throwing program | **fixed** `dcbb7a1`, found building the MASM bed: `.xdata$x` reopened at ALIGN(8) and ALIGN(4) is A2015 to ml64. Always 8 in the ml64 dialect |
| V6 (MASM bed) | **done**, MASM `66baab1`: `tests/cpp11/` - four programs in both dialects; the ml64 dialect's code is ml64 14.44's byte for byte; the COMDAT dialect assembles with COMDAT sections and, on the box, links with link.exe and runs as recorded. Step 4 of `tests/run.sh`. No MASM `src/` change |
| V7 (LINK bed) | **done**, LINK `c5548c7`: `tests/cpp11/` (not `tests/probes/`, whose `*.asm` ml64 assembles and cannot for COMDAT) - seven programs: the four, a two-file COMDAT program, CRT-by-directive, a static local; both linkers, both images run, pediff summary recorded. `known-differ.txt` untouched: a CRT image differs from link.exe's by ~130 KB of layout by design, so the run is held, not the bytes |
| LINK fault (found by D15) | **fixed in LINK `src/`**, `e8d51b5`: a SECREL was measured from the section holding the relocation, not the target's - and a thread-local must be measured from the TLS template's start; and no TLS directory was written. Every cpp11 static local with a constructor (`_Init_thread_header` reads `_Init_thread_epoch` through gs:[58h] and a SECREL: link.exe writes 4, LINK wrote 0x22AE4) faulted 0xC0000005 - 8 of 379 cases. **LINK's seal (`link-1.0.dat`) now differs: LINK wants a version bump and reseal at release** |
| 09-23 Shalimar segfault | **closed with LINK `947f143`** (2026-09-23, weak externals settled before the default libraries: every shci program died before main). `directive-crt` in the bed holds that shape and runs |
| D9 / R6 / S4 | **fixed** `0b5541e`: `-rts=<dir>` or `<dir>/rts6x.lib` links RTS6x, `-rts=ti` TI's rts6740; unsaid, RTS6x from `RTS6X` or `lib/rts6x-tms6747` beside the compiler unless `CPP11_TI`/`CPP11_TILIB` names TI's (RIDE's rule), TI's otherwise. The link prints `linking against RTS6x, <path>` or `TI's <lib>`. lnk6x is looked for beside the compiler before PATH. README.md/README.1ST: not edited here (WS-A's files) - text for them below |
| D8 (Driver) | **fixed** `0b5541e`: `-version` says `Built <date time>, the build machine's local time` - no zone it cannot know; `--help`'s `-g` says DWARF on x86_64-linux and arm64-darwin, CodeView on x86_64-windows in the gnu spelling, refused for tms6747 and masm/ml64; tms6747's `-g` is refused by name (`is not supported yet`) where it got the MASM text; the examples' banner comment is the current banner |
| D12 | **fixed** `0b5541e`: the success line says `0 errors, N file(s) compiled without -O because they use 'volatile'` when it happened |
| P1 | **fixed** `a6078a2`: `Source::fail`, `Source::fromFile` and the four backends' refusals throw `CompileFailed` after writing under `diagnosticLock()`; `compileCaught` catches it per job, removes the half-written output; every job runs, the verdict is after `join()`. **`Preprocessor::fail` still calls `std::exit(1)`** - `src/Preprocessor.cpp` is WS-D's; the one-line change is `std::exit(1)` -> `compileFailed()` (and lock the write) |
| P2 | **fixed** `4be1374`: `static const int t = ...` |
| P4 | **done** `2a4ff01`: `make tsan` (g++ `-fsanitize=thread`, libtsan by rpath - no `libtsan.so.0` on the box's loader path) and `tools/tsan-check`. **It found a real race**, fixed `ab7939e`: `__DATE__`/`__TIME__` call `localtime` (and `tzset`) from every worker thread; the macros are now made once on the main thread. Result at the end |
| V9 | **done** `e2b4a7f`: `tests/tms6747.sh` exports `CPP11_C6XLIVECHECK=1` (`CPP11_C6XLIVECHECK=` unsets it) |
| R9, F38 | design notes below |
| WS-A's Driver findings | `--help`, the `-g` refusal, `-version`'s PST, the examples' banner - all in `0b5541e` |

## Gates, measured (Windows box; the build at `4df8afc` plus the uncommitted P4 harness, i.e. everything but `ab7939e` and `66ed5f1`)

- Windows cases, `run-cases.cmd` both spellings, the project's masm and the **fixed** LINK
  (built from LINK `review/f-windows`): **-O0** GNU 379 printed their output / 0 failed, 226
  refusals / 0 failed; MASM 379 / 0, 226 / 0. **-O2** the same, both spellings. (Before the
  fixes: MASM refused 10; after the Masm.cpp fixes, 8 static-local cases crashed under LINK.)
- `tests/tms6747.sh` -O0 and -O2, both legs, livecheck on: 388 passed, 0 failed each.
- emit golden 0 of 1582 changed; MASM golden 75 changed, every one classified above.
- `tools/exclusions --check`: 131 sites, 0 uncited, 0 stale. `make comments`: 0.
- `tools/seal check`: 12 sealed files differ - expected, every one changed here or by WS-A.
- MASM bed: `tests/run.sh` all passed (Linux box, step 4 included); `tests/windows/cpp11.cmd` on
  the box: all four ran as recorded, ml64 code as recorded. LINK bed on the box: `run.sh` 10
  matched, 9 known, 0 differed; `tests/windows/cpp11.cmd` all seven programs ran as recorded
  under both linkers; `bad.sh` all cases (Linux box).
- P1 on the box: two files with an error each, `-j 4` - both diagnostics, exit 1; four files,
  three wrong - three diagnostics, the good file's `.s` written, exit 1.
- D9 on the box: auto-detected RTS6x beside a copied cpp11, `-rts=dir`, `-rts=ti` - each `.out`
  printed `hello 42` on sim6747; `-rts=C:\nowhere` refused by name.
- **Not run: `winlink-check`** (stopped before it).

## What moved in the emit golden

`tests/emit.sh --record` at `cc210bb` on the box, then the same at the branch's tip:
**0 of 1582 files changed** - the default x86_64-windows spelling is GNU, so the golden carries no
MASM. A MASM golden of the branch's own was recorded beside it (`-S -arch x86_64-windows
-masm=masm`, every case, 379 files): **75 changed**, every changed line one of
`.data`/`.bss`/`.rdata SEGMENT ... COMDAT` -> `_DATA`/`_BSS`/`CONST` (10, 22, 170 pairs) or an
`EXTERN $LNleave$...` removed (31). The ml64 dialect changes only `.xdata$x SEGMENT READONLY
ALIGN(4)` -> `ALIGN(8)`.

## Not done, and why

- `Preprocessor::fail`'s exit - WS-D's file (above).
- `README.md`/`README.1ST` for `-rts` - WS-A's files. Suggested text: "*-rts= picks the C6000
  run-time library: a directory holding RTS6x's rts6x.lib, or the .lib itself, or `ti` for TI's
  rts6740. Unsaid, cpp11 links RTS6x when `RTS6X` names it or `lib/rts6x-tms6747` is beside
  cpp11.exe - as RIDE installs it - and no CPP11_TI or CPP11_TILIB names TI's; TI's rts6740
  otherwise. The link line says which.*"
- The tms6747 `-g` refusal is printed by the driver, so `tools/exclusions` (which reads
  `fail(...)` calls) does not count it; EXCLUSIONS.md (WS-A's) may want a hand-written entry.
- `winlink-check` was not taught the MASM spelling - it links cl's halves with cxx1's, both
  through link.exe; the MASM spelling's seam is the case run above.
- On the box MASM's `tests/run.sh` reports `errors.txt` different (CRLF from the cl build); on
  the Linux box the whole run, the new step included, passes. LINK's `bad.sh`
  `absolute-path-input` fails under Git bash only (a `/c/...` path read as a switch); on the
  Linux box every case passes.

## P4, TSAN - half done, and the Linux gates skipped

The Linux box was given up on the user's instruction mid-round. One `make tsan` run completed
before that, on the build before `ab7939e`: it found **two real races** - `localtime`/`tzset`
from every worker (`__DATE__`/`__TIME__`, fixed `ab7939e`) and `OptTable.cpp`'s opcode index filled
lazily by two jobs at once (fixed `66ed5f1`, with Driver's vcvars cache, the same shape on the tool
pool). Its pool step wrote 0 files per target, uninvestigated. **Skipped on Linux**: the re-run
of `make tsan` after both fixes, `run.sh` -O0/-O2 with the g++ build, `make test` there.

## Stopped here (wind-up, 2026-10-08)

- **Done and gated**: everything in the table above through `4df8afc`, MASM `66baab1`, LINK
  `e8d51b5`/`c5548c7`.
- **Done, built nowhere**: `ab7939e` (macros once on the main thread) and `66ed5f1` (two statics
  made by initialiser) - small, reviewed by reading, not compiled or run on either box.
- **Half done**: P4 (above); `winlink-check` not run.
- **Untouched**: nothing else in WS-F's list. Nothing is running on the Windows box.
- **Merge order for the main session**: LINK `review/f-windows` before this branch's verify-three
  leg, or the MASM pass's 8 static-local cases fail under master LINK.

## Draft CLAUDE.md sections

**"The MASM spelling under the suite, and the two linker faults it found."** The project's masm
refused 10 of 379 cases: a COMDAT data block named `.data` (the .DATA directive) - named
`_DATA`/`_BSS`/`CONST` now, the objects byte-identical - and an early exit's `jmp $LNleave$`
that made a funclet label an EXTERN. With both mended, 8 static-local cases crashed under LINK:
a SECREL measured from the relocating section instead of the target's TLS template, and no TLS
directory. run-cases.cmd runs both spellings by default; MASM's and LINK's beds hold cpp11's
EH and COMDAT objects. The ml64 dialect refused every throw (A2015, `.xdata$x` at two aligns).

**"-rts and the runtime named."** As RIDE: RTS6x beside the compiler unless TI's is named.

**"A worker never exits."** `CompileFailed` per job; every file's diagnostic; verdict after join.
TSAN found `localtime` and a lazily filled static map shared by the jobs.


## Design note R9: why the C6000 optimizer is a text pass, and what `Mir` would have to carry

**Declined this round, as the plan says; the small half (V9) landed.** `src/optimizer/Mir*.h` is an
x86-64 IR: a stream of two-operand instructions over physical registers numbered below
`kFirstPseudo`, webs and pseudos, a colouring allocator, and the flow `OptFlow` rebuilds. Moving
`C6xSched.cpp` and `C6xPipe.cpp` onto it would need, at the least:

- **units and sides** as a property of an instruction, not of its spelling: the `.L/.S/.M/.D`
  choice, the A and B register files, and the 1X/2X cross paths - today `C6xModel.h`'s
  `unitsFor` and `fits` read them off the mnemonic and the operand text;
- **delay slots** as an edge latency the IR knows (a load's four, a multiply's three, a branch's
  five), so a write's *landing* and its *issue* are two different points - `Mir`'s liveness is
  instantaneous;
- **execute packets** - a `||` group is one cycle, read-all-then-write-all; a value written in a
  packet is not seen by a reader in the same packet, which no x86 pass assumes;
- **predication** on any instruction (`[A1]`, `[!B0]`), with a write that may or may not happen -
  `Mir`'s webs assume a definition happens;
- **register pairs** (`A5:A4`) that are one value in two registers, and the pair rules the
  pipeliner's renaming obeys.

Every one of those is a change to the IR every x86 pass reads, for one target. The text pass
was chosen because the backend already emitted finished C6000 assembly, and the scheduler and
pipeliner could be measured instruction by instruction against cl6x's output and TI's
simulator without touching the x86 path. Its known cost is the one V9 now guards: liveness is
read off text that passes edit, so a pass that edits and then trusts old liveness answers about a
text that no longer exists. `CPP11_C6XLIVECHECK` holds every pass that vows its edits only
shrink liveness (`LivenessHeld`) to that vow, and `tests/tms6747.sh` now turns it on for every
run. The day the C6000 needs a pass the text form cannot hold - a global register allocator
across blocks, say - is the day to give `Mir` the five things above, starting with delay slots.

## Design note F38: the Windows exception pieces still refused, and the shape of each

Kept refused by name (`.notarget` reasons, measured on the box at this branch: the cases with a
`.notarget` for x86_64-windows are the ones below and the layout ones). The class-typed throw
the review lists landed on 2026-09-26 and is not refused any more.

- **Rethrow** (`throw;`, `rethrow.notarget`). On this ABI it is `_CxxThrowException(nullptr,
  nullptr)`, raised from inside the handler funclet; the runtime finds the exception being
  handled from its own per-thread state. What is missing is measurement, not syntax: cl's
  listing for a rethrow inside a catch, and what the FH3 tables must say about the funclet's
  state so the outer `try`'s handler is found and the inner catch's object is destroyed exactly
  once. Write it from cl's `/FAs` for three shapes - rethrow to the caller, to an enclosing try
  in the same function, from a nested funclet - and the run.
- **A `try` inside a `catch`** (`try-in-handler.notarget`). A nested handler is a funclet
  inside a funclet; cxx1 names funclets `<fn>$catch$N` from one counter per function, which is
  why ml64 said A2005. The FH3 tree already numbers nested states (2026-09-26); what is missing
  is a funclet emitted while another is being cut out of the output, and its `dispFrame`
  (56 for every catch funclet, measured) measured again for a funclet whose parent frame is a
  funclet's.
- **A destructible local inside a handler** (`local-in-handler.notarget`): the cleanup
  funclet would live inside the catch funclet - the same nesting.
- **A terminating funclet** (`dtor-throws-unwinding`, `catch-copy-throws`, the `noexcept`
  cases): cl marks a funclet whose unwind must terminate; cxx1 writes no such state, so a throw
  out of a destructor during unwinding is caught where the standard says terminate. The
  Itanium targets have it as a catch-all row whose pad calls `std::terminate`.

Not one of these is a day's work beside D15, which is why they stay refusals; each is a
measurement against cl first, then code.
