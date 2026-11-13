# Design review: "Compiler Design - cpp11 2" (Draft 1) against "Design Requirements" (Draft 2)

Reviewer: Claude (Fable 5.1), 2026-10-22. Read-only review; nothing in the three source trees was changed. Every `file:line` below was read during this review; paths are relative to `/Users/g.r.akhtar/Developer/Claude/GRIDE1/` unless given in full. Statements marked **(inference)** are my reading, not something the code states.

## 0. Method and evidence base

- Read in full: `Compiler_Design.md` (148 lines), `Compiler_Design_Requirements.md` (68 lines).
- cpp11 1.28 (`toolchain/Compilers/C++Optimize/src`, 66,348 lines in 106 files, 60 of them `.cpp`): read `Ast.h`, `Type.h` (TypeTable), `Source.h`, `Lexer.h`, `Version.h`, `Driver.cpp` 1205-1230, `parser/Parser.h` 1-120 and the template-body machinery, the cited lines of `ParserOverload.cpp`, `ParserTemplate.cpp`, `ParserStmt.cpp`, `ParserExprNew.cpp`, `ParserConst.cpp`; all of `optimizer/{Mir.h, MirAlloc.h, OptIr.h, OptFunction.h, Optimizer.h, Inliner.h, OptLoops.h, C6xModel.h}`, the heads of `MirAlloc.cpp`, `OptPipeline.cpp`, `C6xSched.cpp`, `C6xPipe.cpp`, `A64Peep.cpp`, `OptValues.cpp`; `backend/{Backend.h, X86_64Windows.cpp, Tms6747.h, Arm64Darwin.h, Masm.h, X86_64Linux.h, Walker.h, Spelling.h}`.
- cppparse v6 (`examples/projects/cppparse`, 38,075 lines): `middle/Ir.hpp` in full, `Ssa.hpp`, `Ssa.cpp` head and function list, `Optimizer.cpp` 270-290, 598-610, 1108-1125, 1229-1280, `IrGen.hpp` head, `IrGen.cpp` 4980-4995; `backend/{Coff.hpp, Coff.cpp head and encoder, Lower.hpp, Lower.cpp 38-60, X86.hpp, X86.cpp 368-392 and its EH lines}`; `frontend/{Sema.hpp head, CxxAst.hpp head, Pp.cpp directive handling, SemaExpr.cpp 285-300 and 608-625}`; `analyzer/Parser.cpp` 290-340, `analyzer/Compiler.cpp` head, `report/*` listing; `common/Parallel.hpp`; `driver/main.cpp` head.
- compilerpp (`examples/projects/compilerpp`, 13,738 lines): `Parser.h` virtuals, `Parser1.h` in full, `Lower1.h` head, `Semantic.h` head, `Semantic.cpp` 796-812, `Lexer.cpp` 104-143, `Bytecode.h` head and limits, `VM.h` head, `VM.cpp` trap sites and UB policy, `main.cpp` flags.
- Git: `git show --stat cf1e607`.
- **Experiment.** Built cpp11 1.28 from source with `clang++` into the scratchpad (`-std=c++11` fails; `-std=c++14` builds all 60 files) and ran the resulting binary over its own 60 `.cpp` files with `-S -O0 -arch arm64-darwin`, then repeated with scratch *copies* of the source carrying the trivial fixes the requirements name, three waves in all. Results in section 4 and Appendix A. No file in the source trees was modified.

## 1. Citation check

Verdicts: **confirmed** (the cited place says what the design says), **partly** (true with a qualification that matters to the design), **wrong**, **not found**.

| # | Design claim (section) | Verdict | Evidence |
|---|---|---|---|
| 1 | cpp11's parser is "34,790 lines today" (3) | confirmed | `wc -l src/parser/*` = 34,790 |
| 2 | cppparse's IR "already numbers blocks and temporaries" by index (2) | partly | `middle/Ir.hpp:43-62` `IrOperand{kind,id}`; `IrBlock::id` at 84. But every `IrInst` carries `std::string sub` (69), blocks carry `std::string label` (85), operands a `std::string name` (49): it is an array-of-structs with strings, not the SoA/handle model of sec. 2 |
| 3 | "cpp11's and cppparse's tables already intern" (2) | partly | cpp11 `Type.h:55,603,609` ("interned by TypeTable on key", `intern(DepExpr)`); cppparse `Sema.hpp:3` "Types are interned canonical objects", `key()` at 60. Neither has a string/identifier table: cpp11 keys everything by `std::string` (`Parser.h:33,70,91`, `Lexer.h` `Token::text`) |
| 4 | `Driver.cpp:1218` volatile disables -O (4) | confirmed | `Driver.cpp:1218-1230`, `level = program.usesVolatile ? 0 : optimize_` at 1223 |
| 5 | `MirAlloc` is Chaitin-Briggs after GCC's IRA (5) | confirmed as stated, **wrong as applied** | `MirAlloc.h:3-5`, Briggs test `MirAlloc.cpp:259`, coalesce 279, simplify 305, "Chaitin-Briggs over the graph" 382. But it is x86-64 only: registers are `enum {RAX..R15}` in `OptIr.h:17`, offer list `kOffer` of x86 names at `MirAlloc.cpp:41`, `candidate(r)` r < `kGprs` at 14. The design's "same" for tms6747/arm64 (sec. 5 table) has no basis in code |
| 6 | cpp11 has a Machine IR, "cpp11's `mir`" (5) | partly | `namespace mir` exists (`Mir.h`, `MirAlloc.h`) but operates on `opt::Instr { std::string m; Operand a, b; }` (`OptIr.h:66-74`): a parsed x86 **assembly text stream** behind the `Spelling` sink (`Optimizer.h:3` "-O1 and -O2, as a spelling in front of the spelling"). There is no target-neutral machine IR |
| 7 | cpp11 `OptJumps`, `OptDivide`, `OptHoist`, `OptLoops`, `Inliner.h` as reusable passes (4) | partly | All exist (`optimizer/`). `OptPipeline.cpp:57-73`: hoisting and counter widening require `kPropPhysical` (run after allocation, on physical x86 registers); `Inliner.h:5` works on AST nodes and "the walker". The ideas port; the code is x86-stream-specific |
| 8 | `ParserOverload.cpp:978` lookup/ADL (3) | confirmed | 978 comment cites `[basic.lookup.argdep]`; `lookupKeys` at 980; ADL cases 999, 1018 |
| 9 | `ParserOverload.cpp:603, 842` ICS ranking/tournament (3) | confirmed (±6 lines) | 601-602 "[over.match] reduced to what rung 2 needs"; 613 `isPromotion`; 848-850 best viable function |
| 10 | cppparse `SemaExpr.cpp:293` implicit conversions (3) | confirmed | 288 `Conversion Sema::convertType(...)` |
| 11 | cppparse `SemaExpr.cpp:616` deduction with partial explicit args (3) | confirmed | 616 `Sema::deduce`, `m_fixedTemplateParams` at 620 |
| 12 | `ParserTemplate.cpp` token replay (3) | confirmed | `ParserTemplate.cpp:2,849,868,2833,2890`; `Parser.h:310-334` `PendingBody`, `replayInlineBodies` |
| 13 | cppparse `IrGen.cpp:4989` vtables/vtordisp (3) | confirmed | 4988-4995 vtordisp `$4` thunks |
| 14 | compilerpp `Semantic.cpp:804` override/hide checks (3) | confirmed | 805-806 `resolveOverrides`, "different signature ... warns" |
| 15 | compilerpp refusal table `Lexer.cpp:108-141` (3) | confirmed | `reservedWordHelp` 112-141 |
| 16 | compilerpp `Lower1.h:13` cleanups at every block exit (3) | confirmed | `Lower1.h:14` "a local dying -> a destructor call at every exit from its block" |
| 17 | cpp11 `Ast.h` `Try`, `FuncletLeave` (3) | confirmed | `Ast.h:38-39, 510, 536` |
| 18 | cppparse `Ir.hpp:64-105` `IrFunction/IrBlock/IrInst` (4) | confirmed | lines 64-134 |
| 19 | cppparse `Ssa.cpp`: Cytron (dominators, frontiers, placement, renaming) + GVN (4) | confirmed, with caveats | `Ssa.cpp:35` frontiers, 55 `Renamer`, 180-212 placement, 314 `gvn`; `Optimizer.cpp:175` dominators. Caveats: SSA is a late, optional phase on a memory-based IR (`Ir.hpp:4-7`, `Ssa.hpp:3-11`); GVN is **disabled** in any function with `try` (`Optimizer.cpp:1248`), and the whole SSA result is **discarded** if a handler needs a value (1265-1268); 395 lines in all |
| 20 | cppparse `Optimizer.cpp:274-605` -O1 passes (4) | confirmed | 274 `constantFold` ... 605 `simplifyCfg` |
| 21 | cppparse `Optimizer.cpp:1117` register promotion with exceptions in mind (4) | partly | 1117-1122 `pinHandlerLocals`: specific to cppparse's **setjmp/longjmp** exception model (`X86.cpp:233, 767, 1318, 1345`). It does not transfer to table-based EH |
| 22 | cppparse LICM, unrolling (4) | confirmed | `Optimizer.cpp:737 licm`, `922 unrollLoops` |
| 23 | linear scan at `Lower.cpp:44`, `X86.cpp:374` (5) | partly | `Lower.cpp:44` `kIntRegs = 6` is for "VM64", a virtual target (`Lower.hpp:1-6`); `X86.cpp:375 computeIntervals` and `X86.hpp:3-4` are the real x86 linear scan |
| 24 | cpp11 `C6xSched`, `C6xPipe` list/modulo scheduling (5) | confirmed as function, **wrong as layer** | `C6xSched.cpp:1-3`, `C6xPipe.cpp:1-3`. Both operate on `std::vector<Line>` where `Line::raw` is a **text line** (`C6xModel.h:20-40`); liveness is recomputed from text (`C6xModel.h:94-97`) |
| 25 | cpp11 `Tms6747` selection, "C64x+ compact forms" (5) | confirmed | `Tms6747.h:3-5` ("serial, a stack machine over A4, delay slots as NOPs"); usage text says C64x+ 16-bit compact instructions |
| 26 | cpp11 `X86_64Windows` selection (5) | partly | `X86_64Windows.cpp` is 75 lines of ABI table; code generation is `X86_64Linux` (GNU syntax) or `MasmCodeGen` (`X86_64Windows.cpp:68-74`) |
| 27 | `__CxxFrameHandler3` tables and funclets exist; rethrow and try-in-catch missing (5) | confirmed | `Masm.cpp:474-475`; `ParserExprNew.cpp:1142-1147` (rethrow refused on x86_64-windows); `ParserStmt.cpp:1438-1443` (try inside catch refused) |
| 28 | `.dwpsn` for C6000 (5) | confirmed | `Tms6747.h:48`; 3 hits in `Tms6747.cpp` |
| 29 | cppparse `Coff.cpp`: REX/ModRM/SIB, REL32/ADDR64/ADDR32NB, `.pdata/.xdata`, COMDAT, CodeView `.debug$S` (5) | confirmed, **with a structural caveat** | `Coff.cpp:4-14`, relocation enum 270, `emitRm` 373. Caveat: it is a **MASM-text assembler** ("writes an x86-64 COFF object file directly from the MASM text produced by X86.cpp", `Coff.hpp:1-3`) for a listed instruction subset (`Coff.hpp:5-8`; 26 distinct `mn ==` forms). It is not an `MInst` encoder. cpp11's x86 walker emits 73 distinct mnemonics including x87 (`fld`, `fstp`, `fildq`, `fucomip` ...) and `rep`, none of which Coff.cpp encodes |
| 30 | since `cf1e607` masm writes CodeView and link writes a PDB (5) | confirmed | `git show cf1e607`: "masm: CodeView in .debug$S ... link: /DEBUG writes a PDB" |
| 31 | compilerpp VM 50M-step cap (6) | confirmed | `Bytecode.h:55` `MachineMaxSteps = 50L * 1000 * 1000` |
| 32 | cppparse `common/Parallel.cpp` worker pool (7) | partly | exists; "on Windows the pool uses CreateThread, elsewhere it falls back to running the jobs sequentially" (`Parallel.hpp:3-4`). cpp11 **already has** `-j` on `std::thread` (`Driver.cpp:27, 151, 1297, 1352, 1405`), so the attribution is backwards |
| 33 | cpp11's 37 headers; `<memory>` notes `make_shared`, `weak_ptr` missing (7) | confirmed | `include/` has 39 entries, 2 internal (`__string_conversions`, `__tree`); `include/memory:8-10` |
| 34 | cppparse code families "E/W/I/T" (8) | partly | codes found: S 154, T 85, E 31, W 25, I 4. Sema uses **S** codes (`Sema.hpp:9`, `SemaExpr.cpp:1852` "S146"); the design omits the largest family |
| 35 | `(cl C2027)` in cppparse (8) | confirmed | `SemaExpr.cpp:1852` |
| 36 | "cppparse evaluates no `#if` today" (8) | partly | True of the **analyzer** (`analyzer/Parser.cpp:295-300` records the condition text, never evaluates); **false** of the front end (`frontend/Pp.cpp:229-233, 550` evaluates `#if`/`#elif`) |
| 37 | cppparse reports in text, HTML, PDF, SVG (8) | confirmed | `driver/main.cpp:10-23`, `report/{Report,Html,Pdf,Diagram}.cpp` |
| 38 | compilerpp `-ast/-ir/-bc` (1) | confirmed | `main.cpp:76-79, 120-123` |
| 39 | GRIDE's `toolchain.json` calls cpp11 (1) | confirmed | `GRIDE 2.0/toolchain.json:7` markers `cpp11.exe` |
| 40 | compilerpp `Parser1` derives from `Parser` through virtual hooks (3) | confirmed, **scale caveat** | `Parser.h:162-171` (7 virtuals), `Parser1.h:22, 58-65`. The C++ layer is a **subset** that refuses templates, exceptions, namespaces, `operator`, `static`, `enum`, `union`, `typedef`, `sizeof` (`Lexer.cpp:113-141`) |
| 41 | "cpp11 1.26 temporaries' lifetimes" (3) | confirmed | `Version.h` 1.26 entry |
| 42 | Baseline "2 of 60 compile; 28 aggregate, 29 headers" (9) | confirmed exactly | Appendix A: 2 OK (`Operator.cpp`, `OptTable.cpp`); 21 `Op` + 7 `BaseSpec` = 28; headers 16+9+2+1+1 = 29; 1 `LineFields` |
| 43 | Stage 0 "checked with `clang -std=c++11`" (9) | **wrong today** | `clang++ -std=c++11` fails on `Source.cpp:140` (`SubstitutionFailure{...}`, NSDMI at `Source.h:14-18`) and `Type.h:377` (`BaseSpec{...}`); `-std=c++14` builds |
| 44 | "14 probes" (10, R9.2) | not found | `macos/help/manual/10-part10-cpp-conformance.md` speaks of one probe per feature; no countable set of 14 found in the tree |
| 45 | `docs/OPTIMIZER-IR.md`, `docs/EXCLUSIONS.md` referenced from code (`Mir.h:3`, `Version.h`, the compiler's own error text) | not found | no `docs/` under `C++Optimize`; `find` over GRIDE1 finds neither file |

Headline: 45 claims checked; 25 confirmed outright, 15 partly (the qualification matters to the design in 9 of them: rows 5, 6, 7, 19, 21, 24, 29, 32, 36), 2 wrong (5-as-applied, 43), 2 not found, 1 confirmed-with-scale-caveat (40). The pattern: the design cites real code accurately but repeatedly mistakes **what layer the code works at** (text streams, setjmp EH, MASM assembler) for the layer the design needs (target-neutral IR, table EH, instruction encoder).

## 2. Requirements traceability

| R | Design section(s) | Verdict | Note |
|---|---|---|---|
| R1.1 nothing working in cpp11 is lost | 1, 11 ("each milestone keeps every gate") | partly | No regression oracle is defined for the behaviour cpp11 has today; the conformance suite (M0) is "grown from the 14 probes", not from cpp11's existing behaviour. M3-M5 replace parser/codegen/optimizer wholesale (sec. 5) |
| R1.2 beats all three on every measure | 10 | unmet | Section 10 defines gates, not measures against the three compilers (compile speed, code speed, diagnostics). "Beats cppparse on `--health`" or "beats compilerpp's VM" is never operationalised |
| R2.1 target order; release needs 1 and 2 | 5 (table in order), 11 (M4 "target 1, then target 2") | partly | M4 moves target 2 to the new IR; between M3 and the end of M4, target 2 either runs on the old path (two optimizers alive) or does not release. Not addressed |
| R3.1 C++11 complete; `__cplusplus` gating | 3 (refusals table) | unmet | The design never mentions `__cplusplus` or what "the C++11 suite passes" means; today's banner is "C++03 with C++11 extensions" (`Version.h` `CXX1_BANNER`) |
| R3.2 C90 through the same front end | 3, 11 M6 | met on paper | Two-layer parser; see 5.2 for why the inheritance mechanism is the weak form of this |
| R3.3 refusals by name with a fix | 3 | met | Table of feature → message → hint |
| R4.1 stage list | 1 | met | |
| R4.2 Sema leaves parser; one SSA optimizer | 3, 4 | met on paper | 5.3: cppparse's middle end is not the base it is presented as |
| R4.3 C and C++ as two layers, C++ derived | 3 | met on paper | 5.2 |
| R4.4 own COFF writer; masm/clang optional | 5, 11 M5 | partly | cppparse's writer is a text assembler for a subset (row 29); the design claims a shared `MInst` path. masm optional only on target 1 |
| R4.5 bytecode VM for constexpr and `--run` | 6, M7 | met on paper | 5.6: wrong layer for constexpr |
| R4.6 `--dump`, `--health`, parallel compile | 1, 8, 7, M8 | met | `-j` already exists in cpp11 (row 32) |
| R5.1 written in C++11 only; checked with clang -std=c++11 | 9 | **conflicting with reality** | Row 43: the check fails today, so "until R5.2 holds" starts from red, and the design's step 1 cannot run as written |
| R5.2 four stages; 2 == 3 | 9 | met | Order of -O0 then -O2 matches |
| R5.3 only own headers; replacements listed | 2 (`os` module), 7 | partly | `os::Pool` "over Win32" (sec. 2) makes `-j` Windows-only; conflicts with R5.6 (self-host on 3 and 4). Wave 2 of the experiment shows `<cerrno>` and `<chrono>` are also missing from the list |
| R5.4 remove C++14 aggregate rule | 11 M1 | partly | The doc names `Op`, `Type::BaseSpec`; waves 2-3 find also `SubstitutionFailure` (`Source.h:14`), `Itanium::Sub` (`Mangle.cpp:197`), `Member` (`Type.cpp:219`) — and the real blockers are compiler gaps, not source (section 4) |
| R5.5 every construct the source needs supported on target 1 | 5 ("rethrow and try-in-catch added"), M1 | partly | Rethrow and try-in-catch are named. Waves 2-3 add: braced default arguments, `std::function` / function types as template arguments (`include/functional:4-18` states `std::function` is absent), `unique_ptr != nullptr`, a lambda reference-capture bug, a qualified base-class `operator=` call. None is in the design |
| R5.6 self-host on 3 and 4; 2 never a host | 11 M9 | partly | Win32-only `os::Pool` and `CoffEmitter` for target 1 only; stages on 3/4 are not described |
| R6.1-R6.3 no `new`/`delete`; by value or container; index handles | 2 | met on paper | 5.1 |
| R6.4 `make_owned` | 2 | met | |
| R6.5 raw pointers only at OS boundary | 2 | met on paper | `os` module |
| R6.6 build gate; module-by-module; count only down | 2 ("Gate"), M2 | partly | Gate described; "converted module by module" contradicts R6.3 for cross-module references (5.1). Count check: 1,143 `new` tokens (1,109 outside string literals), 23 `delete` — R6.6's 1,111/23 is right in substance |
| R7.1 keep link.exe, CRT, SDK, cdb | 7 (implicit) | met | |
| R7.2 target 2 self-contained | 5 (asm6x, lnk6x) | met | |
| R8.1 one-line diagnostic + hint | 8 | met | |
| R8.2 cl error numbers on target 1 | 8 | met | |
| R9.1 self-host is the first gate | 11 M1 | met on paper, **risky** | Section 4 and 5.7: the cost of M1 is unknown and dominated by missing compiler features |
| R9.2 conformance suite from the 14 probes; targets 1 and 2 | 10 | partly | "14 probes" not found (row 44); no C++11 coverage plan |
| R9.3 link with cl objects and CRT | 10 | met | |
| R9.4 sim6747 cycles, no regression | 10 | met | sim6747 exists (`toolchain/TI/SIM6747`) |
| R9.5 0 errors 0 warnings | - | unmet | Not mentioned in the design |

## 3. Tensions inside the requirements

1. **R1.1 vs. R4.2 + R4.3 (and R6.3).** "Nothing working in cpp11 is lost" sits beside three requirements that each remove the structure cpp11's behaviour lives in. cpp11's parser *is* its semantic analyser, its constant evaluator and its lowering: `Parser` owns `Local`, `GlobalSym`, `Signature` (`Parser.h:33-120`), performs overload resolution (`ParserOverload.cpp`), lays out classes and vtables, lowers `try` into `Try`/`FuncletLeave` nodes (`Ast.h:510, 536`) and inlines (`Inliner.h:5` works on `Call` AST nodes). Splitting Sema out (R4.2), re-layering the parser (R4.3) and re-keying every node by handle (R6.3) touches every one of the 34,790 parser lines. That is a front-end rewrite; R1.1 can only be honoured by a behavioural oracle (a recorded test corpus) that does not yet exist. **Risky as written.**
2. **R6.1-R6.3 vs. R6.6.** A handle-only data model is a whole-program property: a node held by `ExprId` must live in the arena that `ExprId` indexes, so a module cannot be converted while its neighbours still pass `const Type *` and `StmtPtr` (`Ast.h` uses `std::unique_ptr` trees; `Type.h` interns `const Type *`). "Module by module" works for *ownership* (replace `new` with `make_owned`), not for *handles*. R6.3 and R6.6 contradict unless R6.3 is scoped to new code. **Contradictory as written.**
3. **R5.3 vs. R1.2 and R5.6.** `<unordered_map>` → `<map>` costs O(log n) on the parser's four symbol indexes (`Parser.h` `functionIndex_`, `globalIndex_`, `typedefIndex_`, `enumIndex_`) — modest, but "beats all three on every measure" (R1.2) now has a self-inflicted handicap. Worse, "`<thread>` behind one Win32 wrapper" makes `-j` Windows-only, while R5.6 wants self-hosting on x86_64-linux and arm64-darwin; cppparse's pool, which the design borrows, "falls back to running the jobs sequentially" off Windows (`Parallel.hpp:3-4`). **Contradictory with R5.6 unless the wrapper has a pthread side.**
4. **R3.1 `__cplusplus` gating.** `__cplusplus` is read by every system header and by cpp11's own library; reporting 199711L while accepting `auto`, lambdas, `nullptr` and variadic templates breaks real headers (`<type_traits>`, MSVC's `yvals.h`) that switch on it and tells self-hosted code the wrong thing. The honest value today is 201103L with a documented exclusions list; gating on a suite makes a *runtime* value depend on a *test* outcome. **Risky; reword (7.1).**
5. **R4.5 VM for constexpr.** C++11 constexpr is defined on the typed expression tree (`[expr.const]`): the evaluator must reject UB, uninitialised reads, out-of-life objects, and must work on template-dependent code at instantiation time with the *target's* layout. compilerpp's VM "defines" UB (`VM.cpp:17-20` wrap, `664-666` out-of-range shift yields 0) and runs on an 8-byte `long` model (`Bytecode.h:30-45`). Feeding it from SSA after lowering loses the information constexpr needs. **Risky; the requirement should ask for the behaviour, not the mechanism.**
6. **R9.1 self-hosting first vs. the architecture.** Self-hosting *before* R4/R6 means getting 66K lines of pointer-heavy, `std::function`-using, lambda-heavy code through a front end that will then be rewritten. The experiment (section 4) shows the blockers are compiler features (R5.5), not source cleanups (R5.3/R5.4); each is paid once on the old parser and then again when Sema moves out. **Risky ordering; see 7.3.**
7. **R2.1 vs. R4.2.** "A release needs targets 1 and 2 passing" plus "one optimizer for all targets" means target 2 cannot be released between leaving its text scheduler (`C6xSched`, `C6xPipe`, 4,300 lines) and reaching parity on the new IR, unless both paths are kept alive. The requirements do not say which. **Needs a decision.**
8. **R1.2 vs. R5.3/R6.** A compiler forbidden `<unordered_map>`, `<thread>`, hand allocation and heap polymorphism is being asked to beat compilers that use all of them. Fine as a discipline, but then R1.2 should name the measures.

## 4. Baseline check and the self-compile experiment

The requirements' baseline (2 of 60; 28 aggregate; 29 headers split 16/9/2/1/1; 1 `LineFields`) **reproduces exactly** with a clang-built cpp11 1.28 on macOS targeting arm64-darwin at -O0 (Appendix A, wave 1). Two notes on it:

- The header figures count *files that fail*, not files that include: only 2 files include `<unordered_map>` directly (`Parser.h` and one more), 2 `<mutex>`, 2 `<atomic>`, 1 each `<regex>`, `<deque>`, `<thread>`. Fixing `Parser.h:14` and `Source.h:3` clears 25 of the 29.
- `'LineFields' was not declared` (`C6xModel.h:36`) is a **compiler bug**: `LineFields::operator=(o)` — a qualified call to the base class's assignment operator — after `: LineFields(o)` on the previous line parsed fine. It is not source cleanup.

Then I did what the design does not: fixed the named items on scratch copies and looked at the **next** error.

| Wave | Scratch change | Compile | New first-error classes |
|---|---|---|---|
| 1 | none | 2/60 | as the doc |
| 2 | ctors on `Op` and `BaseSpec`; shim `<unordered_map>` (over `map`), `<mutex>`, `<atomic>`, `<thread>` (runs inline), `<deque>` (over `vector`) | 2/60 | "a braced default argument is not supported yet" (`Ast.h:238`, 30 files); "a function type as a template argument, `R(Args...)`, is not supported yet" (`OptIr.h:74` `std::function<void(Spelling&)>`, 22 files; cpp11's own `include/functional:4-18` says `std::function` is absent on purpose); three more NSDMI aggregates (`Source.h:14`, `Mangle.cpp:197`, `Type.cpp:219`); `<cerrno>`, `<chrono>` missing; `CodeViewTypes.cpp:128` lambda `[&]` capture of a `const bool` refused as "bool &" (compiler bug) |
| 3 | `Ast.h:238` default spelled out; ctor on `SubstitutionFailure` | 3/60 | "'bool' and 'std::nullptr_t' cannot be combined like that" at `Ast.h:359` `value_ != nullptr` on a `std::unique_ptr` (library or compiler gap), 27 files |

Three waves, three new compiler-side gaps each time, and the parser's 17 files have not yet reached their own first line of code. **(inference)** The number of such gaps is unknown and probably in the dozens: cpp11's source is written in the C++11 the compiler does not yet accept (`std::function`, lambdas with `auto` parameters-by-reference, `unique_ptr` comparisons, braced defaults, `thread_local`, variadic templates in `EntryOf<...>`). Because cpp11 stops at the first error (`Source::fail` is `[[noreturn]]`, `Source.h:56`), the design's "work for milestone M1" has no measurable size. That is the single largest uncosted item in the plan.

## 5. Critical review of the architecture

### 5.1 Section 2 - data model

**What is right.** Handles numbered in creation order give the determinism that stage 2 == stage 3 needs; cppparse already found this necessary (`Coff.hpp:9-10` "laid out in order, so the output is deterministic"; `analyzer/Compiler.cpp:10` "merged in a fixed order"). 32-bit source locations are good. A single `Diagnostics` sink is good.

**What is wrong or unproven.**

1. *It is a rewrite of every data structure the compiler has.* cpp11's AST is a `unique_ptr` tree with a virtual `Visitor` of 29 node kinds (`Ast.h:40-80`); its types are `const Type *` interned in `TypeTable::types_` (`Type.h:582-633`); its symbol tables are `std::string`-keyed maps (`Parser.h`). cppparse's AST is a pointer arena (`CxxAst.hpp:7` "owned by a CxxArena", deleted in bulk at `CxxAst.cpp:120-128`). Neither source has a handle-and-SoA tree; "cppparse's IR already numbers blocks" (row 2) is the only precedent, and it carries strings per instruction. The design presents arenas+handles as a continuation; it is a new dialect for 100K lines.
2. *The dialect is hostile to the compiler that must compile it.* `struct ExprId { uint32_t index; }` with one handle type per arena means dozens of trivial structs, templates like `Arena<T>` with `operator[](Id<T>)`, and `switch`-on-kind with payload lookups in side vectors. That is exactly the template-and-trait-heavy code the experiment shows cpp11 cannot yet compile (function types as template arguments, braced defaults). Virtual nodes (what cpp11 has now) are in fact *easier* for cpp11 to compile than typed-handle generics. The design makes self-hosting harder in the name of a rule unrelated to self-hosting.
3. *Debuggability.* A handle is an integer; a debugger shows `index = 4712`. cdb (R7.1) cannot follow `ExprId` into its arena without a natvis/`.natvis`-equivalent that cdb lacks. With `unique_ptr` trees the debugger shows the node. The design should at least provide `--dump` early (it does, M8 - too late; see 5.8).
4. *"No hand-managed memory" is over-read.* R6.1's intent is "no `new`/`delete`/owning raw pointers": `std::unique_ptr`, `std::vector<std::unique_ptr<Node>>`, `std::shared_ptr` all satisfy it without changing a single data structure. R6.3 (handles) is a separate, much costlier choice presented as if R6 forced it. Zig and Carbon chose arrays for cache behaviour on million-line inputs; cpp11 compiles 60 files.
5. *The `os` module.* Replacing `<thread>/<mutex>/<atomic>` with "worker threads over Win32" is justified only by R5.3 (the library lacks them). The alternative that satisfies R5.3 *and* R5.6 is to **implement** `<thread>`, `<mutex>`, `<atomic>` in cpp11's library over Win32/pthreads (they are small: the compiler uses `lock_guard`, 3 atomics, `hardware_concurrency`, `join` - see Appendix A shim sizes). Then the compiler's source stays portable C++11 and the library gains three headers users need anyway.
6. *"Converted module by module" (R6.6, M2) cannot be done for handles* (tension 2). Ownership can be converted incrementally; cross-module handle types cannot.

### 5.2 Section 3 - parser and Sema

1. *Virtual-hook layering (compilerpp) does not scale to C++11.* compilerpp exposes seven virtuals (`Parser.h:162-171`) and its C++ layer refuses templates, exceptions, namespaces, overloaded operators, `typedef`, `sizeof`, `enum`, `union` (`Lexer.cpp:113-141`); it has never parsed a declarator that C++ and C read differently. In full C++11 the C grammar is *not* a subset to extend by hooks: `T(x)` vs. function-style cast, `a < b > c` vs. template-id, `int(*p)(int)` vs. `new int(*p)`, elaborated specifiers, `typename`, `decltype`, attributes, ref-qualifiers, trailing return types all change productions the C layer owns. Either the C layer grows virtual points on most productions (and is then a C++ parser with a C mode), or the C++ layer re-implements them (and R4.3's "derived" is nominal). cpp11 today handles C and C++ in one parser with `target_`/dialect flags (`Lexer.h:27-29` `isChar`: "int in C and char in C++"); that is the proven shape. **Recommend:** one parser with a `Dialect` parameter and C90 tests, not inheritance. If R4.3 is kept for its teaching value, scope it to the lexer and the C declaration grammar only.
2. *"Ambiguous node settled by Sema" is incompatible with C++ as specified.* C++ requires the parser to know whether a name is a type, template or value *while parsing* (`[dcl.ambig.res]`, `[temp.names]`): `a * b;`, `x < y > z`, `T::U * v` cannot be deferred without parsing both alternatives and keeping both trees (what GCC's tentative parse and Clang's `TryParse*` do - and both consult the *symbol table* during parsing). "Parse before lookup" front ends exist (Elsa/Elkhound GLR, Clang's `-fsyntax-only` is not one), but they produce ambiguity forests that Sema must disambiguate, not single nodes. The design's parser "only builds syntax: no lookup, no types (R4.2)" is a stronger claim than R4.2 makes ("semantics leave the parser"). cppparse itself, which the design cites as the split, keeps a `classNames`-style type table in the parser and annotates in place (`Sema.hpp:9-10`; compilerpp `Parser1.h:38-42` keeps `classNames` for exactly the `(x)` ambiguity). **Recommend:** the parser keeps a *declarative* scope of names and kinds (type / template / value / namespace), filled by the parser itself from declarations, and Sema does everything else. That is what every working C++ front end does.
3. *Token replay for templates.* It works today (`ParserTemplate.cpp`), and keeping it is pragmatic. But three consequences should be in the design: (a) two-phase lookup is approximate - non-dependent names in a template body are not checked at definition time, and errors surface per instantiation, in the instantiating context, with a diagnostic pointing at the template; (b) SFINAE (`Source.h:56-60` trials) is implemented by *re-parsing with failure*, so every candidate costs a parse; (c) once Sema is a separate pass over a syntax tree, replaying *tokens* means the parser runs inside Sema (instantiation happens during Sema, which must call the parser, which must then call Sema on the new subtree). The design's clean "Parser → Syntax tree → Sema" arrow is a loop. A typed template tree (parse the body once into syntax; instantiate by substituting on the tree) is the alternative; it is more work up front but is what makes the Sema split real. The design should choose explicitly and show the control flow.
4. *The Sema module table* is good decomposition. But the `Constexpr` row delegates to the VM (5.6), and the `Classes` row lists vtordisp/virtual bases - cppparse's `IrGen.cpp:4989` is MSVC layout code tangled with its x86 module (`IrGen.hpp` includes `X86.hpp`; the cited code calls `x86.addReturnThunk`). Not liftable as is.

### 5.3 Lowering and the SSA IR (sections 3-4)

1. *cppparse's middle end is not a sound base.* The IR is "three-address with memory locals ... so the optimizer can forward stores to loads and remove dead stores **without an SSA construction**" (`Ir.hpp:4-7`). SSA is a late phase bolted on (`Ssa.hpp:3-11`), 395 lines, run after local passes, undone after, disabled around `try` (`Optimizer.cpp:1235-1236, 1248, 1265-1268`). Instructions carry `std::string sub` for the opcode variant and callee name, types are `const Type *` of the *old* front end (`IrGen.hpp:5-6` "The IR still carries the old front end's Type objects, so sema types are converted through an adapter"). Exception handling is `trybegin/tryend/catchparam` region markers (`Ir.hpp:25-27`) lowered to **setjmp/longjmp** (`X86.cpp:1318, 1345`). The passes are correct for small programs and clearly written, but the design's -O1/-O2 columns borrow ideas that cpp11 already implements on its own stream (jump threading, magic division, hoisting, widening, inlining with budgets), and will have to re-implement them on the new IR regardless. The right description is "a new SSA IR, designed with reference to cppparse's and LLVM's," not "cppparse's IR, extended."
2. *Block arguments vs. phi.* Block arguments are cleaner for SSA destruction and for `invoke`-style edges (the unwind edge can carry values). The churn is nil here because the IR is new anyway (point 1); the design's framing "instead of phi nodes" while "SSA construction stays Cytron's (cppparse `Ssa.cpp`)" is inconsistent - Cytron's renaming inserts phis; with block arguments the renaming writes branch operands. Minor, but it shows the code was not studied for fit.
3. *One IR for MSVC funclets and Itanium landing pads.* `invoke` with normal/unwind successors models Itanium cleanly. MSVC EH needs: catch handlers as separate funclets with their own frames reached through the parent frame pointer (`Masm.cpp:719-729` lifts them as "a slice of the ordinary output"), `FuncletLeave` continuations (`Ast.h:510`), state numbers per try level, `__CxxFrameHandler3` tables, and the known holes (rethrow from a funclet; try inside catch, `ParserStmt.cpp:1438-1443`). LLVM solved this with `catchswitch/catchpad/cleanuppad/catchret` *in the IR* (not plain invoke edges) precisely because funclet boundaries must be visible to every pass (no value may flow into a funclet except through the frame). The design needs those constructs or an explicit outlining step before optimisation. As written, "invoke-style EH edges" under-specifies target 1, the first target.
4. *Lowering output.* `Lowering` is listed as producing SSA IR directly from the typed tree. SSA construction from a typed tree with locals is a known recipe (allocas + mem2reg or Braun et al.'s on-the-fly construction); the design should pick one, since "cppparse's `Ssa.cpp`" promotes *IrLocal*s after the fact.

### 5.4 Section 5 - back ends, one allocator, C6000 scheduling

1. *"Allocation: same" is false for 2, 3, 4.* `MirAlloc` is hard-wired to x86-64 registers (row 5) and to x86 operand forms (`MirAlloc.cpp:19-27` `copyOf` on `movq`). tms6747 today has **no graph-colouring allocator at all**: a stack machine over A4 with a two-walk heuristic that puts the most-used scalars in A10-A13/B10-B13 (`Tms6747.h:3-5, 130-135`) and spill-register planning per push (`Tms6747.h:141-158`); arm64 has a text peephole (`A64Peep.cpp:13-25`). The design must say: "a new target-parameterised allocator, with `MirAlloc`'s Briggs machinery as the model."
2. *C6000 needs a different machine model, not a different column.* Two register files with cross-path limits (`C6xModel.h:46-49` `side`, `cross`), unit assignment (`.L/.S/.D/.M`), predication registers, delay slots whose results land cycles later (`C6xSched.cpp:30-45`), and modulo scheduling with register renaming across iterations (`C6xPipe.cpp:1-3` "renamed per iteration where a value outlives II"). A Chaitin-Briggs allocator *before* scheduling fixes registers and destroys the scheduler's freedom; scheduling before allocation (as TI's compiler and most VLIW compilers do: schedule on virtual registers with register-pressure awareness, then allocate with the schedule fixed, then iterate) is the usual order, and modulo scheduling needs **modulo variable expansion** or rotating registers, which a generic allocator does not know. The current text-level approach is in fact *post-allocation* scheduling on physical registers, with renaming hacks. The design should state: target 2 = selection → (pre-pass scheduling on virtuals + pressure heuristic) → allocation on A/B files with side constraints → post-pass packetising and modulo scheduling → NOP padding. That is a separate allocator.
3. *Order of migration.* Target 2 has 4,300 lines of text-based C6000 optimisation that measurably matter (`Tms6747.cpp:1009` cites 158k → 352k cycles when a hoist went wrong). R9.4 forbids regressions. Moving target 2 onto a new MIR means re-deriving every peephole and the pipeliner against sim6747 cycle counts before it may ship. M4 budgets this as half a milestone.

### 5.5 Emitter: "-S assembled by masm must equal -c byte for byte"

Realistic **only** if both sinks are the same encoder: i.e. the project's masm (`toolchain/Windows/MASM`) and `CoffEmitter` share the instruction-encoding and relaxation code. Otherwise equality fails on: choice of encoding for the same instruction (`mov r64, imm32` sign-extended `C7 /0` vs. `B8+r imm64`; `83 /x ib` vs. `81 /x id`; `add` vs. `lea`), short/near jump relaxation order (Coff.cpp relaxes in `finish()` at line 344 after encoding long; masm iterates), section ordering and alignment padding, COMDAT selection and associative-section order, symbol-table order, CodeView record ordering and checksums (`cf1e607` added CodeView to masm independently of cppparse's `.debug$S` writer), and the timestamp field of the COFF header. Also cppparse's encoder covers a 26-mnemonic subset without x87, `rep movs`, `cmov`, TLS segment forms (row 29) while cpp11 emits 73 distinct mnemonics including a full x87 `long double` path. **Recommend:** state the test as "the two outputs disassemble to identical instruction sequences and identical relocation/unwind/COMDAT *semantics*" (a `dumpbin`/own-disassembler diff), and only promise byte equality if masm is refactored onto the same encoder library - which is a reasonable and attractive step given the project owns masm.

### 5.6 Section 6 - VM for constexpr from SSA

Wrong layer, for four reasons the design does not address:

1. **UB must be diagnosed, not defined.** `[expr.const]/2` makes an expression non-constant if evaluation would invoke UB (overflow, out-of-bounds, null deref, shift ≥ width, reading an indeterminate value). compilerpp's VM explicitly *defines* overflow to wrap and out-of-range shifts to 0 (`VM.cpp:17-20, 664-666`); its traps (`VM.cpp:40-275`) are memory-safety traps, not C++ constant-evaluation checks. The result would be `static_assert(INT_MAX + 1 < 0)` passing.
2. **Typed-tree information is lost by SSA.** Constant evaluation needs object identity, subobject bounds, lifetimes, `const`-ness, which `this` points at, whether a glvalue designates a constexpr object. After lowering to loads/stores on a flat byte vector (`Bytecode.h:8-13` one flat memory; sec. 6 "its memory is one `std::vector<std::uint8_t>`; addresses are offsets") those are gone. Clang's `ExprConstant.cpp` and GCC's `constexpr.c` work on the typed tree for this reason.
3. **Templates.** Constant expressions are needed *during* Sema - for `static_assert`, array bounds, template non-type arguments, SFINAE on `enable_if<cond>` - including inside partially-substituted templates. The VM runs after Lowering; by then Sema needed the value. The design puts `Constexpr` in the Sema table but implements it after Sema.
4. **Layout.** `sizeof`, `offsetof`-style arithmetic and pointer comparisons must use the *target's* layout (`long` is 4 on target 1, 8 on target 3; `wchar_t` 16-bit unsigned on tms6747, `Tms6747.h:25`). compilerpp's VM fixes `long` at 8 bytes (`Bytecode.h:30-33`).

cpp11 **already has** a typed-tree constexpr evaluator with frames and call entry (`ParserConst.cpp:313-324` `constexprSlot`, `enterConstexprCall`) and the C++11 rule set (`ParserConst.cpp:105-122`). The design throws it away for a mechanism that cannot meet the standard. **Recommend:** keep a tree evaluator for constexpr (it is small in C++11, where constexpr functions are single `return` statements); use the VM for `--run` and as a test oracle, where defining UB is acceptable.

### 5.7 Section 9 - bootstrap

- Stage 0 "built by cl `/std:c++14` and checked with `clang -std=c++11`": the second half fails today (row 43). Once R5.4 is done it will pass; say so, and make the clang check the M0 gate, not a later one, since it is the only cheap way to find C++14-isms before cpp11 has to.
- "Baseline today ... work for milestone M1": section 4 shows the baseline measures the *first* error per file and that the next errors are compiler features. M1 is unbounded as written.
- Stage 2 == stage 3 needs more than address-free handles: no iteration over `std::map<const Type *, ...>` or `std::set<const Node *>` (ordering by pointer) anywhere, no `std::unordered_*` iteration order dependence, no `__DATE__`/`__TIME__` (the driver notes these, `Driver.h:104`), no thread-timing-dependent output order (cpp11's `-j` already keeps order, `Driver.cpp:497`), reproducible floating-point formatting. The design's "handles ... make output independent of addresses" is necessary, not sufficient. Add a determinism gate to M0 (two runs, same output) that is cheap and independent of self-hosting.

### 5.8 Section 11 - milestones

- **M1 before M2-M5 doubles the work.** Everything M1 adds to the *current* parser (braced defaults, `std::function`, `nullptr` comparisons, lambda capture fixes, rethrow in funclets, try-in-catch, dozens more) is written against a parser whose semantic half M3 rewrites and whose codegen M4/M5 replace. The design chooses this because R9.1 says self-hosting is the first gate; but the requirement does not say the gate must be met on the *old* architecture. See 7.3 for a sequencing that satisfies R9.1 at the end of each milestone without paying twice.
- **M2 (arenas + handles) is a big-bang rewrite in disguise.** Tension 2. There is no "ship on its own" state halfway through re-keying a 29-kind `unique_ptr` AST and a pointer-interned `TypeTable`.
- **M3 + M4 + M5 are each rewrites of a ~10-35K-line component with the old one deleted.** "Each milestone ships on its own and keeps every gate" is asserted, not shown: during M4 target 2 cannot pass R9.4 on the new IR until the pipeliner is re-derived.
- **M8 (dumps, `-j`) is last; it should be first.** `-j` exists (`Driver.cpp:151`). `--dump` is how every milestone's work gets debugged and how tests pin behaviour; compilerpp's `-ast/-ir/-bc`, which the design cites, exist for that reason.
- **M7 (VM) after M4**: fine for `--run`; wrong for constexpr (5.6), which Sema (M3) needs.
- **Nothing in M0-M9 retires the 4,300 lines of C6000 text passes or says how `-O2` on target 2 is kept green while target 1 moves.**

### 5.9 Missing from the design

1. **A behavioural oracle for R1.1.** Record, now, cpp11 1.28's output (`-S` for all four targets at -O0/-O2, exit codes and stdout of the probe programs) over a corpus (its own 60 files once they compile, the probes, the examples). Every milestone diffs against it. Without this, "nothing lost" is unfalsifiable.
2. **Second-system risk, stated.** The design merges the best-sounding part of each source without noting that cppparse is strict C++98 with a setjmp EH model and compilerpp is a teaching subset; the parts are not drop-in.
3. **ABI conformance testing on target 2.** R9.3 covers cl objects on target 1; nothing covers mixing cpp11 objects with TI `cl6x` objects and RTS6x (`Tms6747.h:25-34` encodes several "measured from cl6x" facts) - that is the equivalent gate for the second release target.
4. **Name mangling and RTTI** (`Mangle.cpp` 2,063 lines, both MSVC and Itanium schemes; `CodeViewTypes.cpp`) are not placed in any module of sec. 3; they move with Sema's `Classes`.
5. **Diagnostics quality under token replay**: template errors reported where? With what instantiation stack? Clang-style notes need the instantiation chain kept.
6. **Cost of four backends alive during migration** (two optimizers, two emitters per target for a year) - no staffing/sequence statement.
7. **Error recovery.** cpp11 stops at the first error (`Source.h:56`). Self-hosting work, `--health` and IDE use all want all errors in a file; the one-error model is why the baseline undercounts.
8. **Performance budget.** R1.2 asks to beat the sources; nothing measures compile time (the design even removes `unordered_map`).
9. **R9.5 (0 warnings)** and **R3.1 (`__cplusplus`)** are not addressed.

## 6. Verdict and ranked issues

**Verdict: reject in its current form; resubmit as Draft 2 with the sequencing, layering and constexpr decisions below.** The document is a thoughtful synthesis and most of its citations are accurate as far as they go. But in each of the three places where it claims to *reuse* code - the allocator for all targets, cppparse's SSA middle end and COFF writer, compilerpp's VM for constexpr - the cited code works at a different layer than the design needs, and the plan's sequencing (self-host on the old architecture, then rewrite everything beneath it) pays the largest unknown cost twice.

| Sev. | Issue | Recommended change |
|---|---|---|
| **Blocker** | M1 (self-host on cpp11 1.28's architecture) is uncosted and dominated by missing compiler features (section 4), then superseded by M2-M5 | Re-sequence (7.3): self-host gate re-asserted at the end of every milestone on the *new* front end; M0 adds `--keep-going`/all-errors so the gap list is measurable |
| **Blocker** | Constexpr via an SSA-fed bytecode VM cannot implement C++11 `[expr.const]` (5.6) | Tree-level evaluator in Sema (extend `ParserConst.cpp`); VM for `--run` and testing only. Reword R4.5 (7.1) |
| **Blocker** | "Allocation: same" for tms6747: `MirAlloc` is x86-only and C6000 needs schedule-aware allocation on two register files (5.4) | Target-parameterised allocator interface; C6000 pipeline: pre-pass schedule → allocate → modulo schedule; explicit parity gate vs. today's cycles before the text passes are retired |
| **Major** | Arenas + typed handles for every structure, converted "module by module" (5.1, tension 2) | Satisfy R6 with `unique_ptr`/containers and the gate; use handles only in the new IR and the new typed tree; reword R6.3 (7.1) |
| **Major** | Virtual-hook C/C++ parser layering and "parse before lookup" (5.2) | One parser with a dialect parameter and a declarative name-kind scope; C90 as a test suite, not a base class; reword R4.3 (7.1) |
| **Major** | cppparse's IR/SSA/EH presented as the base for the middle end (5.3) | Describe the IR as new; adopt funclet-aware EH constructs for target 1; choose one SSA construction method |
| **Major** | `-S` == `-c` byte-for-byte claim and the COFF writer's coverage (5.5) | Share one encoder library between the project's masm and `CoffEmitter`; test semantic equality first; list the encoder's instruction gap vs. cpp11's 73 mnemonics |
| **Major** | No oracle for R1.1; no determinism gate; no all-targets regression corpus in M0 (5.9) | M0 records cpp11 1.28's outputs for all targets and diffs forever |
| **Major** | `os::Pool` over Win32 only vs. R5.6 (tension 3) | Implement `<thread>/<mutex>/<atomic>` in the library over Win32 and pthreads; keep the compiler source portable |
| **Minor** | Row 32/36/34 misattributions (`-j`, `#if`, S-codes); "14 probes" and `docs/*.md` unlocatable | Fix text; check the docs into the tree or drop the references |
| **Minor** | `__cplusplus`, R9.5, mangling/RTTI, ABI tests on target 2, error recovery not covered | Add sections |

## 7. Alternate design

### 7.1 Requirements I would reword (separate from design disagreements)

| R | Problem | Proposed wording |
|---|---|---|
| R3.1 | A runtime macro gated on a test suite; breaks headers that switch on it | "`__cplusplus` is 201103L. `docs/EXCLUSIONS.md` lists every C++11 construct not yet accepted; the conformance suite's C++11 part is the measure of that list shrinking to empty." |
| R4.3 | Mandates a mechanism (inheritance) that does not fit C++'s grammar | "One parser accepts C90 and C++11; the dialect is a parameter. C90 programs are tested through it; c90's separate compiler retires when its tests pass." |
| R4.5 | Mandates the VM as the constexpr mechanism | "Constant expressions are evaluated at Sema time on the typed tree, rejecting undefined behaviour as [expr.const] requires, with the target's layout. A bytecode VM runs whole programs without linking (`--run`) and serves as a test reference." |
| R5.3 | Forces the compiler's own source off `<thread>/<mutex>/<atomic>` and `<unordered_map>` | "The compiler's source uses only headers its own library provides. The library gains `<unordered_map>`, `<thread>`, `<mutex>`, `<atomic>`, `<chrono>`, `<cerrno>` (and `std::function`) in time for the self-hosting gate; the compiler's source is not rewritten around their absence except where a header is deliberately out of scope (`<regex>`)." |
| R6.3 | Whole-program handle model stated as universal | "New data structures (the typed tree, the IR, the machine IR) use index handles. Existing structures satisfy R6.1-R6.2 through `std::unique_ptr` and containers; they adopt handles when rewritten, not before." |
| R6.6 | "Module by module" is only possible for ownership | add: "Ownership is converted module by module; handle conversion follows component rewrites." |
| R9.1 | "First gate" read as "before the architecture changes" | "Self-hosting on target 1 is a gate every milestone from M1 on must pass at its end; the stage-1 compiler of each milestone is that milestone's own." |
| R2.1 | Silent on how target 2 stays releasable during the middle-end move | add: "While a target is being moved to the new middle end, its previous code path remains selectable (`-fold-backend`) and is the one released until the new path meets R9.4." |
| R1.2 | "Every measure" undefined | "Measures: conformance count, compile time on the self-host corpus, sim6747 cycles on the benchmark set, x86-64 run time on the benchmark set, `--health` findings on the examples. The design states the target value of each." |
| R9.2 | "14 probes" not locatable | Name the file or directory that holds them. |

### 7.2 Pipeline and data model

```
source → Preprocessor → Lexer → Parser(dialect: C90 | C++11; keeps a name-kind scope)
       → Syntax tree (handles; new)
       → Sema (lookup, overload, templates by substitution on the syntax tree, classes/layout/mangling,
               constexpr tree evaluator, checks)  → Typed tree (handles; new)
       → Lower → SSA IR (new; block args; invoke + funclet-aware EH ops; TypeId operands)
       → Optimizer (-O1/-O2 passes written for the new IR; cpp11's and cppparse's passes are the reference)
       → Target: select → MIR (virtual regs, target-described reg files/classes)
            x86-64: alloc (Briggs, lifted from MirAlloc) → peephole → Emitter
            C6000 : pre-pass schedule (virtual regs) → alloc (A/B files, side+cross constraints)
                    → packetise/modulo schedule/NOP pad → Emitter
       → Emitter: one encoder library → {Asm text, COFF object}; masm re-based on the same library
       VM: --run (and test oracle), fed from SSA IR; not used for constexpr
```

Data model: `unique_ptr` trees and interned pointers stay in surviving cpp11 modules (preprocessor, lexer, mangler, library); handles in the three new structures (syntax tree, typed tree, IR/MIR) where they pay for themselves (determinism, serialisation for `--dump`, cheap copies during instantiation). Threads via the library's own `<thread>` etc.

Template handling: parse template bodies once into syntax; instantiate by substituting on the syntax tree inside Sema (no re-lexing), with an instantiation stack kept for diagnostics. If schedule forces it, keep token replay *as a Sema-internal strategy* behind the same interface and switch later; the design must then show the Sema↔Parser re-entry.

### 7.3 Migration path and milestones

Principle: **every milestone ends with the whole compiler self-hosting at -O0 on target 1 and all four targets' `-S` output unchanged against the oracle**, and no milestone leaves two live implementations of one layer for longer than one milestone.

| M | Delivers | Why in this order |
|---|---|---|
| M0 | Oracle corpus (all 4 targets' `-S` at -O0/-O2 on the probes, examples and cpp11's own 60 files via cl where cpp11 can't yet); determinism gate (two runs identical); `--dump` at every existing boundary; `--keep-going` so all errors are counted; clang `-std=c++11` build gate for the compiler's own source; R6 counter | Everything after is measured against this |
| M1 | Library: `<unordered_map>`, `<thread>/<mutex>/<atomic>` over Win32+pthreads, `<chrono>`, `<cerrno>`, `std::function`, `make_shared`/`weak_ptr`; source: remove NSDMI aggregates and `<regex>`, `<deque>`; compiler: braced default args, `unique_ptr` vs `nullptr`, lambda capture fix, qualified base member call, rethrow and try-in-catch on target 1 | The *measured* gap list from M0's `--keep-going`, not three items. Ends with the first self-host (old architecture) - the cheapest point at which R9.1 can first be true, and the one that tells us the real feature gap |
| M2 | Sema split: new syntax tree + typed tree *in parallel* with the old parser behind a flag; the old parser's codegen path consumes the typed tree via an adapter visitor so all four backends keep working | The front-end rewrite, isolated; backends untouched; oracle diff is the gate |
| M3 | SSA IR + optimizer + MIR for **target 1** only; old x86 stream optimizer kept behind `-fold-backend`; parity with the oracle at -O0, then -O2 performance ≥ old on the benchmark set; self-host at -O2 and stage 2 == stage 3 | One target, one new middle end, measured |
| M4 | Encoder library; `CoffEmitter`; masm re-based on the same encoder; `-S` ≡ `-c` test | Independent of M3's optimizer and can run in parallel |
| M5 | Targets 3 and 4 onto the new IR (they are x86-64 and a RISC with a text peephole: low risk); `-fold-backend` retired for 1, 3, 4; self-host on 3 and 4 | Lets the generic MIR/allocator prove itself on two more targets *before* the VLIW |
| M6 | Target 2: MIR for C6000, schedule-aware allocation, port of the packetiser and pipeliner onto MIR; R9.4 parity against the text passes; retire text passes | The hardest back end last, with the most mature infrastructure |
| M7 | C90 dialect through the parser; c90 retires | After the front end is stable |
| M8 | VM `--run` from SSA; `--health` on the typed tree; `-j` already exists | Low risk, no dependencies |

Side by side with the design's plan: both reach the same end state; the alternate (a) never rewrites a layer while the layer below it is also moving, (b) spends M1's compiler work on features that stay (library, EH holes) and pushes semantic fixes into the new Sema rather than the old parser, (c) moves the VLIW target last instead of second, (d) makes R9.1 a per-milestone end gate instead of a prerequisite for starting.

### 7.4 Points of disagreement at the design level, summarised

1. Keep cpp11's shape (one parser with semantic knowledge of names) rather than compilerpp's inheritance split; put *everything else* in Sema.
2. Design the IR fresh; cite cppparse for ideas, not as the base.
3. Constexpr on the tree; VM for `--run`.
4. One allocator *interface*, at least two allocator *strategies* (graph colouring for the RISC/CISC targets; schedule-aware for C6000).
5. Handles only in new structures; R6 satisfied elsewhere by `unique_ptr`.
6. Portable threading in the library; no Win32-only `os::Pool`.
7. Self-host gate at each milestone's end; oracle and determinism gates from day one.

## Appendix A - the self-compile experiment

Build: `clang++ -std=c++14 -O0 -w -c` over the 60 `.cpp` files, linked as `cpp11` in the scratchpad. (`-std=c++11` fails on `Source.cpp:140` and `Type.h:377`, both NSDMI aggregates.) Run: `./cpp11 -S -O0 -arch arm64-darwin -I <include> <file>` per file; the second output line (after the banner) is the first error.

Wave 1 (unmodified source, cpp11's own `include/`): **ok=2 fail=58**

```
 21 error: 'struct Op' writes an initialiser on a member, so in C++11 it is not an aggregate ...   (Spelling.h:70)
 16 #include <unordered_map>   (Parser.h:14)
  9 #include <mutex>           (Source.h:3)
  7 error: 'struct Type::BaseSpec' writes an initialiser on a member ...                             (Type.h:377)
  2 #include <atomic>
  1 error: 'LineFields' was not declared                                                             (C6xModel.h:36)
  1 #include <regex>           (A64Peep.cpp:7)
  1 #include <deque>           (C6xPipe.cpp:11)
  2 OK: Operator.cpp, OptTable.cpp
```

Wave 2 (scratch copies: constructors added to `Op` and `BaseSpec`; shim headers `<unordered_map>` as `map` subclass with `reserve()`, `<mutex>` with `lock_guard`/`unique_lock`, `<atomic>` with `load/store/fetch_add/++`, `<thread>` running the callable inline with `join()` and `hardware_concurrency()`, `<deque>` as `vector` subclass with `push_front/pop_front`): **ok=2 fail=58**

```
 30 error: a braced default argument is not supported yet - write the value                          (Ast.h:238)
 22 error: a function type as a template argument, 'R(Args...)', is not supported yet                (OptIr.h:74, std::function<void(Spelling&)>)
  2 error: 'LineFields' was not declared
  1 error: 'struct SubstitutionFailure' writes an initialiser on a member ...                        (Source.cpp:140)
  1 error: 'struct _GLOBAL__N_1::Itanium::Sub' writes an initialiser on a member ...                 (Mangle.cpp:197)
  1 error: 'struct Member' writes an initialiser on a member ...                                      (Type.cpp:219)
  1 error: a braced default argument is not supported yet                                            (Preprocessor.h:14)
  1 error: 'isUnion' is 'bool &' and this is 'const bool' - a reference that can write cannot bind   (CodeViewTypes.cpp:128, lambda [&])
  1 #include <cerrno>  (Lexer.cpp:5)     1 #include <chrono>  (Driver.cpp:22)     1 #include <regex>
```

Wave 3 (plus: `Ast.h:238` default spelled as `std::vector<int>()`; constructor on `SubstitutionFailure`): **ok=3 fail=57**

```
 27 error: 'bool' and 'std::nullptr_t' cannot be combined like that                                  (Ast.h:359, `value_ != nullptr` on a std::unique_ptr)
 22 (std::function, as wave 2)
  rest as wave 2
  3 OK: Operator.cpp, OptTable.cpp, Source.cpp
```

Counts of memory idioms in cpp11 1.28 `src/`: 1,143 occurrences of the token `new` (1,109 outside string literals), 23 of `delete`, 65 of `unique_ptr`/`shared_ptr`/`make_unique`. Direct includes: `<unordered_map>` 2 files, `<mutex>` 2, `<atomic>` 2, `<thread>` 1, `<regex>` 1, `<deque>` 1.
