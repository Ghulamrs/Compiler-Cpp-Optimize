# RIDE 5.0 - installers and the C6747 toolchain against TI, 2026-10-06

**Status, later the same day:** D1 (lnk6x) and D2 (cpp11 -O0) are fixed and 5.0 is re-released;
the results below are the run that found them. See REPORT-RERELEASE.md beside this file.

Three boxes, each with RIDE 5.0 built from fresh GitHub checkouts and installed. The
installed **cpp11 -> asm6x -> lnk6x -> vm6747sim** was measured against TI's reference,
**cl6x (8.2.2 / 7.4.4) -> TI linker -> CCS 5.5 C6747 cycle-accurate simulator**.

## 1. The installers

| box | installer | built from | installed | verified |
| --- | --- | --- | --- | --- |
| Windows (DESKTOP-0HN6OC7) | `RIDE-5.0-setup.exe`, 5.9 MB, SHA-256 `0a642418...9b2e` | `release.cmd 5.0`, 06:10 PKT: RIDE `3b74708`, VM6747 `779f3eb`, cpp11 `3daf518`, ASM6x `d440851`, LNK6x `342110f`, LINK `a2ff14b`, MASM `d606874`, C2S `9c03700`, VM6747-sim `54ea121` | `C:\Program Files\RIDE 5.0`, beside 4.7 | all 12 programs dated 06:10 today; `--version`: cpp11 1.5 (Built 06-10-2026 06:10:12), asm6x 1.0, lnk6x 1.0, vm6747 1.0, vm6747sim 1.0 |
| Windows installer, copy on the Mac | `~/ride-release/20261006-060951-windows/RIDE-5.0-setup.exe` + `RELEASE.txt` | the same file | - | SHA-256 matches the box's copy |
| Linux (ansicc, AL2023) | `RIDE-5.0-linux-x86_64.run` | `release.sh 5.0`, 06:07 PKT, the same commits (RIDE `8f82c0f`; `3b74708` changed only the Windows .iss); seal check passed | `/opt/ride-5.0`, commands in `/usr/local/bin` | its self-check compiled and ran a program; cpp11 Built 06-10-2026 06:07:21 |
| Mac | `RIDE-5.0-macos.pkg` (01:37 PKT, fresh clones, the same commits) | - | by the user, `/usr/local/ride-5.0` | SampleExt, see section 4 |

**Fixed during the release:** `RIDE-5.0.iss` carried 4.7's AppId, so the first 5.0 setup
upgraded `C:\Program Files\RIDE 4.7` in place. 5.0 now has its own AppId (RIDE `3b74708`,
pushed); 5.0 is installed beside 4.7, and 4.7 was restored with its own 05-10 setup.

**Open:** the Linux installer links every command into `/usr/local/bin` except `vm6747sim`.
The binary is in `/opt/ride-5.0/bin`, and RIDE finds it beside itself. The Mac pkg does link it.

**Linux box cleaned** of 05-10's exercise first: `~/VM6747-sim` (clean, nothing unpushed),
`~/simfix`, `~/simrev`, `~/c822`, and 97 MB of journal. 6.5 GB free.

## 2. The corpus and the method

366 programs: every tests/cases case meant to run on tms6747 (no .error, no tms6747
.notarget, not in tms6747-lp64.txt) = 356, plus the six C6747 kernels and four C programs.
Each image is linked with RIDE's own flat map (`ti-link.cmd`, as compile.cpp writes it:
64 MB DDR, 16 KB stack, 1 MB heap), so what is tested is what the product links.

| variant | compiler | assembler / linker | TI runtime | boxes |
| --- | --- | --- | --- | --- |
| c11-O0/O1/O2/Os | installed cpp11 | installed asm6x + lnk6x | 7.4.4 EH build | Windows (all four), Linux (O2, Os), Mac (O2) |
| ti822-O2 | cl6x 8.2.2 (CCS 7.4) `--exceptions --rtti` | TI 8.2.2 | 8.2.2 EH | Windows |
| ti744-O2 | cl6x 7.4.4 (CCS 5.5) | TI 7.4.4 | 7.4.4 EH | Windows, Linux |
| lnk822-O2 | installed cpp11 (the c11-O2 object) | lnk6x | 8.2.2 EH | Windows |
| tilink822-O2 | installed cpp11 (the same object) | TI 8.2.2 linker | 8.2.2 EH | Windows |

Every linked image was run on the **installed vm6747sim** (`--run -c`) and on the **CCS 5.5
C6747 cycle-accurate simulator** (DSS, `cycle.CPU`): 2,748 TI runs on Windows (12-14 at a
time), 900 on Linux (one at a time, stopped at 08:20 as planned). Each run is compared on:
vm6747sim output vs CCS output (byte for byte), each vs `.expected`, whether each reached
`C$$EXIT`, and vm6747sim cycles / CCS cycle.CPU.

## 3. Results

### Windows, the full matrix

| variant | built | vm6747sim reached exit | vm6747sim = .expected | CCS ran | CCS = .expected | vm6747sim = CCS | cycles sim/CCS median (min-max) |
| --- | --- | --- | --- | --- | --- | --- | --- |
| c11-O0 | 365 | 353 | 333 | 363 | 334 | 362 | 0.9953 (0.870-1.000) |
| c11-O1 | 365 | 353 | 334 | 363 | 335 | 362 | 0.9942 (0.885-1.000) |
| c11-O2 | 365 | 354 | 334 | 363 | 335 | 362 | 0.9942 (0.886-1.000) |
| c11-Os | 365 | 353 | 334 | 363 | 335 | 362 | 0.9942 (0.885-1.000) |
| ti822-O2 | 284 | 283 | 268 | 284 | 270 | 282 | 0.9946 (0.946-1.000) |
| ti744-O2 | 274 | 273 | 260 | 274 | 261 | 273 | 0.9951 (0.949-1.000) |
| lnk822-O2 | 365 | 341 | 336 | 364 | 337 | 363 | 0.9933 (0.882-1.000) |
| **tilink822-O2** | 365 | 364 | **362** | 365 | **363** | 364 | 0.9936 (0.882-1.000) |

### Linux, the lighter matrix - identical to Windows

| variant | built | vm6747sim = .expected | CCS ran | CCS = .expected | vm6747sim = CCS | median ratio |
| --- | --- | --- | --- | --- | --- | --- |
| c11-O2 | 365 | 334 | 363 | 335 | 362 | 0.9942 |
| c11-Os | 365 | 334 | 263 (cut off at 08:20) | 247 | 262 | 0.9940 |
| ti744-O2 | 274 | 260 | 274 | 261 | 273 | 0.9951 |

**Across boxes:** cpp11's assembly is byte-identical on all three boxes for 364 of 365
programs at -O2, and Windows = Linux for 364 of 365 at -Os. The one exception,
`cassert-twice`, differs only in the path `__FILE__` bakes in, and that is defect D3 below.
The cycle counts of the same image are identical on Linux and Windows from **both**
simulators (998 vm6747sim and 894 CCS comparisons). The 6 that differ are cl6x 7.4.4 images,
because the two CCS 5.5 installs emit slightly different code; vm6747sim follows those
differences cycle for cycle.

### vm6747sim against TI's simulator

* **Output:** identical to CCS on every image but two programs. `include-streams` (all 9
  variants, both boxes) writes a file and reads it back. TI's runtime does that over CIO to
  the host's file system, and vm6747sim carries CIO console output but not file I/O, so the
  read-back is empty (**gap G1**). `by-value-tails` built by **cl6x 8.2.2** -O2 is the one
  **wrong answer** vm6747sim gave in about 3,650 comparisons: it prints `79` where CCS and
  `.expected` print `91` (**defect D4**). The cpp11 and 7.4.4 builds of the same program are
  right.
* **Cycles:** of 2,665 Windows images that both simulators ran to `C$$EXIT` with identical
  output, 2,431 are within 1% of cycle.CPU, 147 within 2%, 76 within 5%, and 11 beyond; 44
  are exact. Aggregate 0.9865. The outliers, where vm6747sim counts low: `include-streams`
  0.87-0.95 (the file I/O it does not do), `divide-by-constant` -Os 0.90, `isort` -O1/-Os
  0.92, `virt` 0.944, `align-loops` (TI's code) 0.946-0.949, `pipelined-stepped` -Os 0.949.
* **Failure modes:** on images that are already broken (the lnk6x defect D1), vm6747sim
  stops with "undefined instruction" where CCS runs on into `C$$EXIT`, and once CCS itself
  crashed natively (`tisim_mm_gem_ca.dll` access violation on `stdexcept` and
  `temporary-unwind`) where vm6747sim stopped cleanly. The output up to the fault is the same.
* **Speed:** 2,018 images in 43 s on vm6747sim (16 at a time), against 58 minutes for the
  same images on CCS (12 at a time).

### The TI oracle against .expected - what is wrong, and whose it is

c11-O2 has 28 programs that TI's simulator says are wrong (-O0 29, -O1/-Os 28):
`catch-by-reference catch-copy-throws cleanup condition-declaration-unwind dtor-throws-unwinding
exception-table-large handler-exit-ms lambda-keeps-try-state lambda-return-through-try
local-beside-try new-class-array-placement noexcept-local-terminates noexcept-terminates
noexcept-terminates-callee rethrow string-at throw-class throw-class-pointer-ms
throw-multiple-bases throw-pointer throw-temporary try-catch try-catch-value try-in-handler
try-in-try-body two-try-same-type`, plus `stack-probe` and `template-nontype-numeric`.

* **26 of them are defect D1.** The same cpp11 object, linked by TI's 8.2.2 linker
  (tilink822-O2), is correct on TI's simulator.
* `stack-probe` needs a 256 KB frame, and RIDE's map gives a 16 KB stack. `template-nontype-numeric`
  prints `+inf` where the expected text has `inf` (TI's printf). Both also fail for cl6x 8.2.2
  and 7.4.4, so they are the environment, not cpp11.
* `hoisted-invariants` at -O0 only is **defect D2**.

The TI compilers' own 13-14 differences from `.expected` are theirs: `__cplusplus` is 199711
(`bool`), float formatting, narrowing, and so on. cl6x 8.2.2 refuses 82 programs and 7.4.4
refuses 92 (C++11 the TI compilers do not accept); cpp11 refuses one, `structs.c`, which is
valid C and invalid C++.

## 4. The Mac, light: SampleExt

With the installed pkg, `cpp11 -O2 -> asm6x -> lnk6x` (TI runtime) -> `vm6747sim`:
`SampleExt.out` ran to `C$$EXIT` (status 0) in 329,039 cycles. Its 34 lines are
byte-identical to the VM6747 emulator's run of the same assembly (what Build > Verify
compares) and to clang's native build on the Mac.

The whole corpus at -O2 with the Mac's tools on vm6747sim (14 s) gives 334 of 365 with
RIDE's map and the 7.4.4 runtime, the same number as Windows and Linux; 362 with D1 worked
around (see D1).

## 5. Defects found

**D1 - lnk6x misplaces C++ unwind tables (LNK6x, severity high).** With a link map that does
not name `.c6xabi.exidx` and `.c6xabi.extab` (RIDE's own `ti-link.cmd` does not), lnk6x
places them itself. Each exidx -> extab pointer is computed for one address of `.c6xabi.extab`,
and the section is then laid out at another. In `try-catch`, `main`'s entry points to
`0xC010EEDC`, but `.c6xabi.extab` is at `0xC010F108`. The unwinder reads `.cinit` bytes as a
personality index, and `__TI_targ_set_pr` branches through `$C$SW1` into data. Every C++
program that throws is broken on TI's simulator, and hence on the C6747, with either runtime
(7.4.4: an early `exit(2)`; 8.2.2: a jump into data).

*Evidence:* lnk822 vs tilink822 above (336 vs 362 correct). The same object with the two
sections named in the map is correct with both runtimes on vm6747sim **and on CCS** (63,123
cycles, `C$$EXIT`, all 8 lines). *Workaround:* add `.c6xabi.exidx > RAM` and
`.c6xabi.extab > RAM` to `tiLinkCmd()` in RIDE `src/compile.cpp` and
`packaging/windows/ti-link.cmd`. *Fix:* LNK6x's orphan-section placement must resolve the
exidx PREL31 relocations after final layout. *Not changed today.* Why no suite saw it: the
tms6747 suite runs cpp11's assembly on the VM6747 emulator and never links with lnk6x.

**D2 - cpp11 -O0 loses a double's high word in an assignment through a computed address
(cpp11, severity high at -O0).** `Tms6747::visit(const Assign &)`, `src/backend/Tms6747.cpp:1091`
and `:1081`, saves the value with `push()` / `pop("A4")`, one word, where `pushValue(n.type())` /
`popValue(n.type(), "A4")` are needed. When computing the address calls a helper (e.g. `i % 8`
-> `__c6xabi_remi`), TI's helper clobbers A5. `m[i/8][i%8] = i * 0.5` stores garbage high words.
`hoisted-invariants` -O0 prints `0.0` in place of `35.0 ... 595.0`; the minimal `t1` prints
`0.0 0.0 0.0` for `0.5 5.0 31.5`. -O1, -O2 and -Os are correct. The VM6747 emulator hides it,
because its helpers are native and leave A5 alone (the same class as C2, DPSP's delay slots).

**D3 - `__FILE__` is not escaped (cpp11 `src/Preprocessor.cpp:405`, c90
`Compiler-Ci/src/Preprocessor.cpp:352`).** The file name goes into the string literal as it
stands. On Windows `C:\r50\corpus\x.cpp` becomes `C:<CR>50corpus...`, so `assert()` messages
carry a mangled path. Backslashes and quotes need escaping.

**D4 - vm6747sim miscomputes one cl6x 8.2.2 image (VM6747-sim).** In `by-value-tails.ti822-O2`
(`C:\r50\img`), the 11th value is `79` on vm6747sim and `91` on CCS. Both reach `C$$EXIT`
(19,290 / 19,464 cycles). An instruction form or packet that only 8.2.2 emits there,
probably one of its compact (16-bit) forms, which is where D7 and D8 were found. To find it:
compare `vm6747sim --dis` with TI's `dis6x` over that image, then trace.

**G1 - vm6747sim has no CIO file I/O** (`include-streams`): `fopen`/`ofstream` on the host's
file system fails. This is a fidelity gap, not wrong code.

**I1 - the Linux installer does not link `vm6747sim` into `/usr/local/bin`.**

## 6. Files

This directory: the three result CSVs (one row per program x variant), the analyses, the
harnesses (`r50.ps1`, `r50x.ps1` Windows; `r50.sh`, `report.py` Linux; `analyze.py`) and
the corpus list. Images and logs: Windows `C:\r50\img`, Linux `~/r50/img`.
