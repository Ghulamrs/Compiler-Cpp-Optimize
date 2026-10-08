# WS-H - verification: handover (TI legs pending)

Branch `review/h-verify` in C++Optimize, RTS6x, LNK6x, ASM6x, SIM6747 and VM6747, 2026-10-08.
Nothing pushed or merged. Box work in `C:\cxx1\rtsdiv\ws-h`.

## Done, with what was measured

| item | state |
| --- | --- |
| V16 provenance | `tools/expected-from-clang` + `tools/windows/expected-from-clang.cmd`: every `.expected` compiled by VS clang 19.1.5 (`-std=c++11 -pedantic-errors`), run, diffed. First run 382/402; **ten cases depended on argument evaluation order** (alternative-tokens, array-of-class-destructor, array-static-local, implicit-move, member, mutable-member, noexcept, operator-compound, return-conversion-leaks-temporary, static-local-constructor; typeid/typeid-ms the same shape) - rewritten one call a statement, `.expected` unchanged, cpp11 on the box agrees at -O0 and -O2. Final run **395/402**; the seven explained below. |
| V1 | `tests/preprocess.sh` (+ `tests/preprocess-known.txt`): 19 agree, 2 known, 0 differ on the box. **cpp11 has no `-E`**; `Driver.cpp` is WS-F's, so the patch is `docs/review-2026-10-08/ws-h-driver-E.patch` (tested on the box, not committed to src/). preprocess.sh skips itself, saying so, until it lands. Known: `preprocessor-shift` (deliberate refusal), `stringize-whitespace` (cpp11 writes `"\' \'"` for `#x` of `' '`; [cpp.stringize]/2 says `"' '"` - WS-D's). |
| V3 | `tests/names.sh --reasons`: 206 files, 449 target lines in 12 kinds, 5 unclassified; the box's two Darwin-header failures named as a host limit. |
| four names.sh failures | `.nonames` for pipelined-data-exit, pipelined-reduction (static local), compare-with-zero (memcpy 72 bytes), ternary-index-after-remake (memset), measured per target on the box. names.sh on the box: **402 passed, 2 failed** (the Darwin-header pair); overload.sh **31/0**. |
| D10 | lambda cases out of o2run's exemption and into release-check's sample (lifetime.h copied); `o2run.py no-ti-build` + `docs/VERIFICATION-2026-10-09/no-ti-build.txt`: the 76 cases cl6x 7.4.4 refused (1004-205830), each with its error, and the tms6747 skips with reasons. `tests/corpus.sh` against `tests/c-corpus/BASELINE` (366/58/0, Linux box at cc210bb); names movement, exits 0. |
| S5/V11 LNK6x | **No src change needed**: measured region by region, none of the 22 runtime probes differs in any loaded section - `.c6xabi.exidx` and its CANTUNWIND entries match (done 10-03). `tests/regions.py`; `known-differ.txt` re-pinned per region; an unpinned region fails (proved: one bit in exidx, one in .text, both fail). Box: run.sh **54 matched, 22 known, 0 differed**; bad.sh clean. LNK6x stays 1.1. |
| D17/V10 ASM6x | c13 was ours: no 16-bit L3 `ADD/SUB .L` form. Measured from cl6x 8.2.2, added to `src/compact.cpp`. check.sh **14/14 identical (13 byte-identical)**; c14's byte difference (global symbol order) in `tests/compact/known-differ.txt`. tests/run.sh unchanged (10/10, edge 317/0). Also measured: cpp11's tms6747 corpus is TI's code byte for byte in only 43/794 files - the RS=1 high-register packets ASM6x does not write (README). **ASM6x src changed -> 1.1 at release.** |
| S3/V14 | RTS6x `tools/referee --record [file.md]` (not run). README sentence. |
| S6 | SIM6747 FIX-REQUEST.md: every item closed with its commit. V12: README already says the memory system is not modelled (line 26) - verified, unchanged. |
| V13 | VM6747 `Emulator/README.md`: own C library and EH runtime; RIDE Run is sim6747. |
| R3/R7 | verify-three: Mac step (exclusions --check, comment cap), referee --record in the c6747 leg, a `ride` leg (`tests/test --require-tools`, `CXX1_RIDE_TREE`), seal file by glob (was 1.5's). |

Provenance, the seven left: handler-exit-end-catch, local-in-handler, throw-out-of-handler (ledger `built` counts differ on the Microsoft ABI - elision-dependent pair; balance fields 0 both; all `.notarget` windows); noexcept-local-terminates (whether the stack unwinds before terminate is implementation-defined; `.notarget` windows); typeid (Itanium type_info layout in the case; `.notarget` windows); stdin-gets (UCRT has no gets; `.notarget` windows); **std-move-and-forward: a real bug** - `include/algorithm` defines `reverse`/`reverseSwap` after `#endif` and outside `namespace std`, and cpp11 still accepts `std::reverse` (templates keyed by bare name). `include/` is WS-L's: handed over.

## Stopped here (wind-up order from the main session)

- **Not run (TI legs, as instructed):** release-check, referee --record, c6747-levels, c6747-three, final release-check. `docs/VERIFICATION-2026-10-09.md` (the page) not written - only `no-ti-build.txt` beside it.
- **Linux box stopped on instruction.** Run there before the stop: run.sh -O0 627/0, corpus 366/58/0, run.sh -O2 626/1 (at cc210bb plus the case edits; the one failure was not read). **Skipped for the stop:** reading that -O2 failure, run.sh -O0/-O2 on the final tip, LNK6x/ASM6x make test under g++.
- **Still running on the Windows box when this was written:** `run-cases.cmd` at -O0 then -O2 in `C:\cxx1\rtsdiv\ws-h\cpp` (started from the Mac; its numbers were not collected). 
- Not done: tms6747.sh both legs, RTS6x make check, RIDE `tests/test` (H's gate rows), expected-from-clang/preprocess.sh wired into verify-three.
- Sealed sources changed: **ASM6x src** (compact.cpp). C++Optimize: no src/ change (tests/tools/docs). LNK6x, RTS6x, SIM6747, VM6747: no sealed file changed.
