# The 04/05-10-2026 findings, re-checked: cause, owner, remedy, priority

Written 05-10-2026 against gcc-scheme at `0407df9` (the C6000 size work on `c6x-size-final`
is not included; none of the items below is in code that branch touches except as noted).
The findings are those of `docs/O2-REPORT-2026-10-04.md`, "Findings, 04/05-10-2026", C1-C7 and
T1-T14. This is a report and no source was changed.

**How each was checked.** The job had thirty minutes, so: two items were settled by a run
(C2 by one assembler probe on the Windows box; C1's shape by compiling the case here and reading
the lowering), nine by reading code or the tree, and the rest are taken from the record and say
so. "Record" means the 04/05-10 exercise, whose runs are not repeated here.

## cpp11: defects and gaps

### C1 - a thrown pointer reaches its handler one dereference short on TI's runtime

- **Re-check:** holds, by reading. `src/parser/ParserStmt.cpp:1412` - "a pointer caught is the
  pointer `__cxa_begin_catch` hands back ... the runtimes return the value, not the object's
  address" - is written for every target, tms6747 included; nothing in `src/backend/Tms6747.cpp`
  adjusts it. The record measured the TI runtime handing back the exception object's address on
  both boxes at every level, and the hosts (libc++abi, libstdc++) return the pointer value, so
  the lowering is right on three targets and wrong on the fourth. VM6747's runtime behaves as the
  hosts' does, which is why 362/362 pass there (T4).
- **Cause:** one lowering for two runtime conventions. TI's `__cxa_begin_catch` (rts6740, the
  C6000 EABI's unwinder) returns the address of the exception object for a pointer type where
  the Itanium runtimes return the adjusted pointer value.
- **Owner:** cpp11 (the lowering), and VM6747 (its runtime must return what TI's returns, or it
  keeps hiding the defect - T4).
- **Remedy:** small. In the `isPointer()` branch at ParserStmt.cpp:1412, for `target_.isTms6747()`
  take `*(T *)fromPtr` as the non-pointer branch below it does; then make VM6747's
  `__cxa_begin_catch` return the object's address for a pointer so the suite catches a regression.
  Risk: low, but it must be measured on TI's simulator first - the record's claim about TI's
  runtime was observed, not read from TI's source, and `throw-pointer` plus
  `throw-class-pointer-ms` are the two cases.
- **Priority:** wrong output on the shipping target - first.

### C2 - a float initialised `{1.0}` prints 0 on TI

- **Re-check:** holds, and **the cause is found, and it is not cpp11's.** Compiled
  `narrowing-allowed.cpp` for tms6747 at -O0 and -O2: both emit the right sequence,
  `MVKL/MVKH 1072693248, A5` (0x3FF00000, the high word of 1.0), `MVK 0, A4`,
  `DPSP A5:A4, A4; NOP 1`. `INTSP`/`INTDP` serve `big` and `d`, which are right. Then one probe
  on the Windows box, `C:\cxx1\fable-scratch\dp.asm`, assembled by TI's asm6x 8.2.2 (`-mv6740`)
  and by ASM6x, both disassembled by TI's `dis6x`:

      TI asm6x    02140138  DPSP.L1  A1:A0,A4      ASM6x  02148138  DPSP.L1  A5:A4,A4
      TI asm6x    02140118  DPINT.L1 A1:A0,A4      ASM6x  02148118  DPINT.L1 A5:A4,A4
      SPDP 021000a0, INTSP 02100958, INTDP 02100738 - identical from both

  ASM6x sets the src1 field (bit 15, the pair's low register number 4) on the pair-source
  single-operand .L forms; TI leaves it zero and the C674x reads the pair from src2 alone - so
  TI's simulator decodes ASM6x's word as something other than `DPSP A5:A4` and writes 0, while
  VM6747 reads the `.s` text and is never shown the encoding (T4). `dd = {f}` is 0 because `f`
  is; `d = {1}` goes through `INTDP`, which matches, and is right.
- **Cause:** `ASM6x/src/forms.h:130`, `{ "DPSP", 'L', "pr", 0, 0x04E, { 5, 0, -1 }, ... }` and the
  `DPINT`/`DPTRUNC` rows beside it: the "pr" operand form writes the pair into both source fields.
- **Owner:** ASM6x. Every double-to-single and double-to-int conversion cpp11 emits for the
  C6747 is mis-encoded by the shipped assembler; the 271/317 edge-probe agreement with asm6x
  recorded in CLAUDE.md did not contain a `DPSP`.
- **Remedy:** small - zero src1 for the "pr" single-source .L forms (`DPSP`, `DPINT`, `DPTRUNC`,
  and check `ABSDP`, `RCPDP`, `RSQRDP`, `DPSPU` if listed), then rerun ASM6x's edge probes with
  those mnemonics added. Risk: low; `dis6x` on the Windows box is the oracle and the probe file is
  already there.
- **Priority:** wrong output on the shipping target, in ordinary floating code - first, with C1.

### C3 - infinity prints `+inf` through TI's runtime

- **Re-check:** from the record, not re-run; consistent with C's `printf` leaving the sign of
  infinity to the library. cpp11 emits no formatting of its own for `%g`.
- **Cause / Owner:** TI's `rts6740` `printf`; TI-side, inherent.
- **Remedy:** none in code; document, and let `tests/tms6747.sh` carry a per-target `.expected`
  or a `.notarget` for `template-nontype-numeric` if the TI run is ever made a gate. Trivial.
- **Priority:** cosmetic.

### C4 - native Windows programs write LF where cl's write CRLF

- **Re-check:** from the record; cause not located in the time. `include/ostream:84` writes
  through `std::fwrite` to the CRT's `stdout`, which the MSVC CRT translates in text mode - so a
  plain `fwrite` would give CRLF. That it does not says something opens the stream in binary mode
  or the program is not using the CRT's `stdout` as expected; `lib/stdio.h` and the link line
  (`libcmt`) are where to look.
- **Owner:** cpp11 (its `include/` and `lib/` runtime layer for Windows).
- **Remedy:** small once located (one `_setmode` or one `fopen` mode); risk low. Until then
  comparisons on Windows normalise line endings, as the exercise did.
- **Priority:** release correctness on Windows - a program's output differs from cl's byte for
  byte, which any user diffing against a reference will meet. Second tier.

### C5 - the assembly is CRLF on Windows and LF on Linux

- **Re-check:** holds by reading: `src/Driver.cpp:1096` opens the output with `std::ofstream`
  in text mode, so the host CRT decides. Byte-identical otherwise (record: 377 of 379).
- **Owner:** cpp11. **Remedy:** open the `.s` with `std::ios::binary` if byte-identity across hosts
  is wanted; trivial, no risk (every assembler takes LF). **Priority:** cosmetic.

### C6 - the "Built ... PST" stamp shows UTC on the Linux box

- **Re-check:** holds by reading: `src/Driver.cpp:901` prints `Built %s PST` from
  `__DATE__`/`__TIME__` (`:338`), which are the build host's local clock; the label is a constant.
- **Owner:** cpp11's build. **Remedy:** either print the zone the host reports instead of a
  constant, or set `TZ=Asia/Karachi` in the Makefile (one line) so every box stamps the same
  zone. Trivial. **Priority:** cosmetic; it mis-states the build time on a release binary built
  on Linux, so do it with the next release build.

### C7 - `structs.c`'s `const void *` to `T *`

- Not a defect, confirmed by reading [conv.ptr]: legal C, ill-formed C++; cpp11 refuses it as
  clang does. Nothing to do; the probe program should be written as C++ or excused by name.

## cpp11: the performance gaps

From the record, not re-measured. The tms6747 index of 1.02 against cl6x 7.4.4 and the kernels'
2.6x code size are the subject of `c6x-size-final`, under verification now; nothing here
contradicts the record. The native 1.37x against cl is the `docs/DECLINES` work and is not an
inconsistency.

## Inconsistencies across the toolchain

| # | re-check | cause | owner | remedy | priority |
|---|---|---|---|---|---|
| T1 | fixed 04-10 per the record (installers rebuilt); `LNK6x/lnk6x-1.0.dat` is dated 05-10, so the shipped linker is the sealed one. What is still open is the *process* gap: nothing stops an installer being built from a stale tool | no seal check in the installer build | RIDE-4.7 installer | small: the installer script runs `tools/seal check` of every bundled tool and refuses a mismatch | release correctness - second tier |
| T2 | holds, by reading `LNK6x/src/main.cpp:115`: `-v`/`--verbose` exist, no `--version` | never written | LNK6x | trivial; low risk | cosmetic, but the installer check in T1 wants it |
| T3 | holds, by reading: `VM6747/KNOWN-GAPS.md:39` and `Isa.cpp` lists no `SPLOOP*` | VM6747 models the C674x without the loop buffer, a `STDW` offset form and a second in-flight branch | VM6747 | large (SPLOOP is its own unit of state); the `STDW` offset and the branch fault are medium | only blocks running cl6x -O2 code on the emulator; TI's simulator is the oracle for that. Third tier, document |
| T4 | holds, and C2 shows it is wider than the record says: VM6747 reads assembly text, so it also hides every ASM6x encoding fault, not only runtime differences | by design - the emulator runs `.s`, not objects | VM6747 (document), tools (gate) | medium: make `tests/tms6747.sh` assemble with ASM6x and run the TI simulator on a sample, or at least `dis6x`-compare ASM6x's objects against asm6x's on the corpus once per release | release correctness - it is the only reason C1 and C2 reached a release. Second tier |
| T5 | from the record; `argv[0]` is supplied by VM6747's runtime and not by DSS's loader | emulator convenience | VM6747 | small: match DSS (no `argv[0]`) or document; test programs should not read `argv[0]` | cosmetic |
| T6 | holds by reading: VM6747 writes through the host `stdout` in text mode (`Runtime.cpp:269`) | host CRT | VM6747 | trivial (`_setmode` binary) or normalise in the comparison | cosmetic |
| T7 | from the record; the Windows CCS 5.5 is the reference | two CCS 5.5 installs build different code | TI-side | document; never compare across the two installs | none |
| T8 | from the record; mitigated by the retry in `tools/c6747/o2run` | DSS starts one board at a time | box/environment | document: one DSS session at a time per box (the Linux rule), batch on Windows with the retry | costs time only |
| T9 | from the record; mitigated (2 GB swap, fresh JVM every 40 runs) | the DSS JVM grows | box/environment | keep the mitigation; document | costs time only |
| T10 | inherent: cl6x 7.4.4 and 8.2.2 are C++03 | - | TI-side | document; cpp11 is held to clang there | none |
| T11 | inherent; cpp11 agrees with clang on all 10 | TI's runtime and language version | TI-side | document | none |
| T12 | **done, and the report is now behind the tree**: the five seals (`cxx1-1.5.dat`, `ride-4.7.dat`, `lnk6x-1.0.dat`, `asm6x-1.0.dat`, `vm6747-1.0.dat`) are all dated 05-10 and name no Makefile, vcxproj, CMakeLists or build.cmd; cpp11's is commit `9be7a06` | - | - | update the report's T12 state to "done 05-10" | cosmetic |
| T13 | fixed 05-10 per the record; `RIDE-4.7/Editor.xcodeproj/project.pbxproj` names `ccsworkspace` 6 times | the project was hand-maintained beside the Makefile | RIDE-4.7 | keep it generated (`ide/generate.py`), and have the Mac release build run the Xcode build once | release correctness on the Mac - done |
| T14 | from the record; no `.iss` was found under `RIDE-4.7` in the time, so the script's location is not confirmed here | `PrivilegesRequired=admin` with per-user areas | RIDE-4.7 installer | small: `PrivilegesRequiredOverridesAllowed` or per-machine areas; low risk | cosmetic, harmless |

## Where the report is now inconsistent with itself or the tree

- **T12** says "narrowing the scopes is pending"; it was done on 05-10 in all five repositories
  (the seals above) and in cpp11 by `9be7a06`.
- **C2's "caused by -O2? no - every level"** is right, and its row belongs under ASM6x, not under
  "cpp11: defects and gaps": the compiler's emission is correct at every level.
- **T4's statement** that VM6747 "is right where TI's runtime and cpp11 disagree" is half the
  story: for C2 cpp11 and TI's *runtime* agree and it is the *assembler* that differs. The
  emulator hides the assembler as well as the runtime.
- **C1 and C2 are listed as cpp11 defects seen on "both boxes"**; C1 is cpp11's, C2 is ASM6x's,
  and neither is seen on VM6747, which is the one place the suite runs them - so the suites'
  362/362 is not evidence about either.

## The plan, in order

1. **ASM6x: zero src1 on the pair-source .L forms (C2).** One table row family, a `dis6x` oracle
   already on the box, and it is wrong output in every program converting a double to a float or
   an int on the C6747. Then re-run ASM6x's edge probes with the conversion mnemonics in them.
2. **cpp11: dereference once more in the tms6747 catch of a pointer (C1), and VM6747's runtime
   to match TI's.** Measure on TI's simulator first with `throw-pointer`; then the emulator
   change so the suite can hold it.
3. **Close the gap that let 1 and 2 ship (T4):** once per release, assemble the -O2 corpus with
   ASM6x and TI's asm6x and `dis6x`-compare, and run a sample on the CCS 5.5 simulator. This is a
   tool in `tools/c6747/`, not a compiler change, and it is what turns the emulator's green into
   evidence.
4. **Installer: a seal check of every bundled tool before packaging (T1), with `lnk6x --version`
   (T2)** so the check has something to ask.
5. **C4** when its cause is found - a one-line fix, but the wrong-output class on Windows.
6. The cosmetics in one sweep when convenient: C5, C6, T6, T14, and the report's T12 line.

C3, C7, T7, T8, T9, T10, T11 are inherent or environmental and want only the documentation
they already have.
