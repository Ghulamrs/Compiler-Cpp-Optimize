# Compiler Design - cpp11 2, alternate (Draft A1)

2026-10-22. An alternate to `Compiler_Design.md` (Draft 1), written after Fable 5.1's review (`Compiler_Design_Review_Fable.md`) and a check of that review against the code. Requirement numbers are those of `Compiler_Design_Requirements.md` (Draft 2).

## 0. Authorship

| Role | Who |
|---|---|
| Requirements, source compilers | **G. R. Akhtar** |
| Draft 1 | Claude (Opus 5.5) |
| Critical review of Draft 1 | Claude (**Fable 5.1**): 45 citations checked, self-compile experiment in three waves |
| This alternate, and the verdict on the review | Claude (**Opus 5.5**). I re-read 17 of the review's load-bearing citations; all hold (list in section 1.3) |

## 1. Verdict

### 1.1 On Draft 1: agree with the review - not approvable as written

Three of Draft 1's reuse claims take code from a layer other than the one the design needs:

| Draft 1 says | The code is | Evidence |
|---|---|---|
| "Allocation: same" for all four targets (cpp11 `MirAlloc`) | x86-64 only, on a parsed x86 **assembly-text stream** | `optimizer/OptIr.h:17,74`, `MirAlloc.cpp:40` |
| C6000 scheduling from `C6xSched`/`C6xPipe` | works on **text lines** (`LineFields::raw`, `mnem`, `ops` as strings) | `optimizer/C6xModel.h:20-29` |
| Middle end = cppparse's SSA IR, extended | memory-slot three-address IR, SSA added later; GVN off under `try`; SSA undone when a handler needs a value; EH by **setjmp/longjmp** | `middle/Ir.hpp:1-8`, `Optimizer.cpp:1248,1268`, `backend/X86.cpp:233,767,1318` |
| Object writer = cppparse `Coff.cpp` | an **assembler of cppparse's MASM text**, for a subset with no x87 or `rep` | `backend/Coff.hpp:1-10` |
| constexpr in compilerpp's VM | the VM **defines** overflow and shifts by too much, so it cannot reject undefined behaviour as [expr.const] requires; it also fixes `long` at 8 bytes | `VM.cpp:17-20,664-665`, `Bytecode.h:29-45` |

Draft 1's two other structural choices also fail:
- A C layer extended by a C++ layer through virtual hooks, modelled on compilerpp, which itself refuses templates, exceptions, namespaces, operators, `typedef`, `sizeof` and `enum` (`compilerpp/Lexer.cpp:112-141`).
- A parser that does no lookup at all, deferring `a * b;` and `x < y > z` to Sema as "ambiguous nodes". C++ cannot be parsed that way.

### 1.2 On Fable's review: agree with most of it, but not with all of its fixes

**I agree with:** the citation check, the traceability table, five of the eight requirement tensions, the constexpr analysis, the C6000 allocator analysis, the `-S` = `-c` analysis, the oracle and determinism gates, and handles only in new structures.

**I disagree on five points:**

1. **"M1 pays twice" is overstated.** That holds only if the Sema split is a rewrite. Most of the gaps the experiment found are C++11 conformance items that R3.1 requires anyway: braced default arguments, `std::function` (a function type as a template argument), `unique_ptr != nullptr`, lambda capture, and a qualified base `operator=`. Fixing them is never wasted. They are paid twice only if the parser that holds them is thrown away. So the fix is to **extract Sema from cpp11's parser in place**, not to rebuild it (section 3). The real problem with M1 is narrower: its size is unknown. M0's `--keep-going` solves that.
2. **Fable's own plan rebuilds the front end in parallel.** Its M2 runs a "new syntax tree + typed tree *in parallel* with the old parser behind a flag". That second front end beside a 34,790-line parser is the second-system risk the review warns about. Its plan also contradicts itself: it calls self-hosting on the old architecture a blocker, yet its own M1 still ends with that self-host.
3. **Target 2 last breaks R2.1, and Fable's own rule.** Fable moves tms6747 to M6 behind `-fold-backend`. That keeps two middle ends alive across four milestones, which breaks its own principle of "not longer than one milestone". Target 2 is the second release target. It should reach the shared optimizer second, in stages, so it never has to beat today's cycles in one step (section 5.2).
4. **R3.1's `__cplusplus` gating is sound; keep it.** Fable says reporting 199711L "breaks real headers". But MSVC itself reports 199711L by default unless `/Zc:__cplusplus` is given, and its STL tests `_MSVC_LANG`. cpp11's library is the project's own. Keep R3.1. Add a project macro (`__CPP11_LANG`, say), and say in the release notes which headers read which macro.
5. **One parser and "a declarative name-kind scope" is half of the answer.** The proven form is Clang's: the parser *asks Sema* whether a name is a type or template, and builds through Sema "actions". That also solves token replay without a loop (section 3.2).

### 1.3 Citations I checked again

`OptIr.h:15-19, 64-75`; `MirAlloc.cpp:39-42`; `C6xModel.h:20-40`; `Tms6747.h:1-6`; cppparse `Coff.hpp:1-10`, `Ir.hpp:1-8`, `Optimizer.cpp:1246-1269`, `Parallel.hpp:1-5`, `Pp.cpp:228-234`, `X86.cpp:233,767,1318`; cpp11 `include/functional:1-18`, `ParserConst.cpp:310-325`, `Driver.cpp:1297,1352` (`std::thread`); compilerpp `VM.cpp:15-21, 662-667`, `Lexer.cpp:112-141`, `Bytecode.h:28-46`. All say what the review says. I also found that cpp11 has no C dialect mode today, only a comment on the character-literal rule (`Lexer.h:30-32`), so C90 (R3.2) is new work in every plan.

## 2. The principle: refactor cpp11 in place, one layer at a time

Draft 1 and Fable's alternate both build new structures beside the old ones. This design builds only **one** new thing, the SSA IR and machine IR. That is the one layer no source compiler has in usable form. Everything else is **cpp11's code, moved and reshaped while it keeps compiling itself**:

- Every change is checked by the same three gates, from M1 on:
  - **bootstrap:** stage 2 = stage 3 (R5.2);
  - **oracle:** the `-S` output of cpp11 1.28 for all four targets is unchanged unless the change says why (R1.1);
  - **sim6747 cycles:** no regression (R9.4).
- Self-hosting comes first (R9.1 kept as written), because a self-hosting compiler is the largest test case the project has. Every refactor after M1 is checked by recompiling the compiler.
- No layer has two live implementations for longer than the milestone that replaces it.

## 3. Front end

### 3.1 Shape

```
source → Preprocessor → Lexer ─tokens→ Parser ⇄ Sema ─typed AST→ Lowering → SSA IR
                                        (grammar)  (actions: lookup, overload, templates,
                                                    classes, constexpr, mangling, checks)
```

The parser holds the grammar and nothing else. Sema holds every semantic rule, and the parser calls it through *actions* (`actOnDeclarator`, `actOnCall`, `actOnClassEnd`, ...). The parser asks Sema one question while it parses: *what is this name?* (type, template, value, namespace). This is how Clang separates them. It meets R4.2 ("semantics leave the parser") without a syntax tree that has no types, and without ambiguity nodes.

- **The output is cpp11's typed AST** (`Ast.h`, 29 node kinds and a `Visitor`), extended where lowering needs it. R4.1's "typed AST" is this tree.
- **Templates stay on token replay** (`ParserTemplate.cpp`, `Parser.h:310-334`). Sema's `Templates` module asks the parser to replay a body under a substitution scope, and the parser calls Sema's actions as usual. Parser⇄Sema is a call stack, not a pipeline, so the loop Fable points out (section 5.2.3 of the review) does not arise. Instantiation stacks for diagnostics come from that call stack.
- **The extraction is mechanical and gated.** Each move takes a group of `Parser` members, puts them behind a `Sema` interface and leaves the call sites in the parser. The first group is overload resolution, `ParserOverload.cpp`: lookup at :978, ICS at :603, best viable function at :848. Then come the `ParserTemplate.cpp` deduction and specialisation parts, `ParserClass.cpp`/`ParserVtable.cpp` layout, `ParserConst.cpp` constant evaluation, and `Mangle.cpp`. Each move is one commit that passes bootstrap and the oracle.
- **cppparse's Sema is a reference, not a source.** It contributes `convertType` (`SemaExpr.cpp:288`) and deduction with partly explicit arguments (`SemaExpr.cpp:616`), where cpp11's rung is weaker. Those pieces are ported into the matching module.

### 3.2 C90 (R3.2, R4.3)

One parser, with `Dialect { C90, Cxx11 }` passed to both the parser and Sema. C++ derives from C **in Sema's rule set**, not in the parser's class tree: `SemaC` holds the C rules (conversions, `'a'` is `int`, implicit `int` refused, no overloading), and `SemaCxx : SemaC` overrides them. That is the part of C++ that really is "C plus rules", so R4.3's intent (C first, C++ derived) holds where it fits. The grammar stays single: C90's grammar is not a prefix of C++11's, so the parser's productions take the dialect flag. c90's separate compiler retires when its tests pass through this path.

### 3.3 Constant expressions (R4.5 reworded)

Constant evaluation stays a tree evaluator in Sema's `Constexpr` module. It grows from cpp11's `ParserConst.cpp` (`constexprSlot`, `enterConstexprCall`, :313-324) and uses the **target's** layout. It must reject undefined behaviour (overflow, shifts too wide, out-of-bounds access, indeterminate reads) as [expr.const] requires. C++11 constexpr functions are a single `return`, so the evaluator stays small. The VM is used for `--run` and as a test reference (section 6).

## 4. Data model (R6)

- **Ownership, everywhere, module by module.** `new` becomes `make_owned` / `std::unique_ptr` / containers. The R6.6 gate counts down from 1,109 (the review's count of `new` outside string literals) from M0 on. This change is local, so it really can go one module at a time.
- **Handles only in the new IR and machine IR.** There they give determinism, cheap copies for inlining and unrolling, and `--dump` serialisation. Handles are typed (`ValueId`, `BlockId`, `InstId`), and the IR's own source avoids constructs cpp11 cannot yet compile.
- **The AST and types keep `unique_ptr` and interned `const Type *`.** For determinism (section 7), no container is ordered or hashed by address. The gate is a lint rule: no `std::map`/`std::set` keyed by a pointer, and no iteration over an unordered container that reaches the output.
- **Threads and `<unordered_map>` go in the library, not around it** (R5.3 reworded). The library gains `<thread>`, `<mutex>`, `<atomic>` over Win32 and pthreads, so targets 3 and 4 can host the compiler (R5.6). It also gains `<chrono>` and `<cerrno>`. For M1 it gains an `<unordered_map>` built on the existing `__tree`, which is correct, needs no change to the compiler's source, and can become a hash table later. `<regex>` and `<deque>` leave the source: there is one use of each.

## 5. Middle and back ends

### 5.1 SSA IR (new; R4.2)

- Functions, blocks, **block arguments**, typed values (`TypeId` from the existing `TypeTable`), and `volatile` as a property of a load or store (cpp11 today turns -O off: `Driver.cpp:1223`).
- **SSA construction:** lowering gives each local a stack slot. A `mem2reg` pass promotes slots whose address is not taken. This is the shape of cppparse's `loadlocal/storelocal` promotion, done once and kept in SSA form.
- **Exceptions:** the IR has `invoke` with normal and unwind successors, and **funclet pads** in the style of LLVM: `catchswitch`, `catchpad`, `cleanuppad`, `catchret`. Target 1's `__CxxFrameHandler3` funclets are therefore visible to every pass, and Itanium (targets 3 and 4) lowers the same pads to landing pads. The rethrow and try-in-catch holes (`ParserExprNew.cpp:1142`, `ParserStmt.cpp:1438`) are closed in M1 on the current back end, because the compiler's own source needs them. The pads re-express them; they are not a precondition.
- **Passes** are written new on this IR. cpp11's text passes (`OptJumps`, `OptDivide`, `OptHoist`, `OptLoops`, `Inliner`) and cppparse's (`Optimizer.cpp:274-605`, LICM :737, unrolling :922) are the specification and the oracle. A port is done when its `--dump=ssa` effect and the benchmark numbers match the old pass.

### 5.2 Machine IR and the targets

- **MIR** holds virtual registers, register classes and files from a target description, and instructions as `MInst` with operand kinds. It is not text.
- **Two allocation strategies behind one interface:**
  - *graph colouring* (Chaitin-Briggs) for targets 1, 3 and 4. It is `MirAlloc`'s machinery (Briggs test :259, coalescing :279, simplify :305) lifted off `RAX..R15` onto target register classes;
  - *schedule-aware* for C6000: pre-pass scheduling on virtual registers with a register-pressure limit, then allocation on the A/B files with side and cross-path constraints, then packetising, modulo scheduling (with modulo variable expansion) and NOP padding.
- **Target 2 moves in three steps, so R2.1 and R9.4 hold at every release:**
  1. *(M4a)* C6000 selection reads the SSA IR instead of the AST, and still produces today's text. `C6xSched`/`C6xPipe` keep running as post-passes on that text. Target 2 gets the shared optimizer (R4.2) without its cycle-critical code changing.
  2. *(M4b)* Selection produces MIR. Allocation is schedule-aware. The output text still goes through the old post-passes.
  3. *(M6)* The packetiser and pipeliner are ported onto MIR. The text passes are deleted only when sim6747 cycles on the benchmark set are equal or better.

### 5.3 Emitter (R4.4)

- **One encoder library** turns `MInst`s into bytes, with relaxation, relocations, `.pdata`/`.xdata`, COMDAT and CodeView. It serves two sinks: an `AsmEmitter` that writes text, and a `CoffEmitter` for target 1.
- **The project's own masm is re-based on the same library.** Then `-S` assembled by masm equals `-c` byte for byte by construction, and the test catches divergence in the *text* path.
- **cppparse's `Coff.cpp` is the starting reference** for the encoder: REX/ModRM/SIB at :373 and the relocation kinds at :270. It is extended to cover the 73 mnemonics cpp11 emits, including x87 and `rep`.
- **Before the masm re-base,** the test is that both outputs disassemble to the same instructions, relocations, unwind information and COMDATs.

## 6. VM (R4.5 reworded)

compilerpp's stack machine, fed from the SSA IR, is used for:
- `--run`;
- the "VM reference" test, where `--run` output must equal the target program's output.

Its definitions of overflow and shift behaviour are fine for these uses. Its word width and layout are set by the selected target, not fixed at 8 bytes. It is not used for constexpr (section 3.3).

## 7. Diagnostics, determinism, bootstrap

- **Diagnostics:** one sink in the R8.1 format. cl numbers where they match (R8.2). Codes keep cppparse's S/T/E/W/I families. `--keep-going` (all errors in a file, not the first) comes in M0, because self-hosting work cannot be sized without it (cpp11 stops at the first error today: `Source.h:56`).
- **Determinism gate (M0):** two runs, same bytes, on every corpus. It is independent of self-hosting and catches pointer-ordered containers, `__DATE__` and thread-order output early.
- **Bootstrap:**
  - Stage 0 is cl `/std:c++14`, as R5.2 says.
  - `clang -std=c++11` becomes a gate in M1, once the NSDMI aggregates are gone. The review found it fails today (`Source.cpp:140`, `Type.h:377`).
  - Stages 1-3 follow R5.2.

## 8. Milestones

| M | Delivers | Gates added |
|---|---|---|
| M0 | Oracle corpus: `-S` for all 4 targets at -O0/-O2 on the probes, examples and cpp11's 60 files (built by cl where cpp11 cannot yet); `--keep-going`; determinism gate; R6 counter; `--dump` at the boundaries that exist today | oracle, determinism, R6 count |
| M1 | **Self-hosting at -O0 on target 1**, on cpp11's own structure. The source loses NSDMI aggregates, `<regex>` and `<deque>`. The library gains the headers in section 4. The compiler gains the gap list that M0's `--keep-going` measured: braced defaults, `std::function`, `unique_ptr`/`nullptr`, lambda capture, qualified base `operator=`, rethrow, try-in-catch, and whatever else the list shows | bootstrap; `clang -std=c++11` |
| M2 | Sema extracted from the parser in place, module by module (section 3.1); diagnostics sink; `make_owned` conversion continuing | each move: bootstrap + oracle |
| M3 | SSA IR, passes, MIR, Briggs allocator for **target 1**; the old x86 text optimizer is deleted at the end of M3; -O2 self-host; stage 2 = stage 3 at -O2 | -O2 benchmark ≥ 1.28 |
| M4 | **Target 2** on the shared optimizer: steps 1 and 2 of section 5.2 | sim6747 cycles ≤ 1.28 |
| M5 | Encoder library; `CoffEmitter`; masm re-based; `-S` = `-c` (can run in parallel with M3/M4, as it touches only the emitter) | byte equality |
| M6 | C6000 packetiser and pipeliner on MIR; text passes deleted | cycles ≤ M4 |
| M7 | Targets 3 and 4 on the new IR; library threads over pthreads; self-hosting there (R5.6) | bootstrap on 3, 4 |
| M8 | C90 through the parser and `SemaC`; c90 retired | c90's tests |
| M9 | VM `--run` and the VM reference test; `--health` on the typed AST; `-j` already exists (`Driver.cpp:1352`) | VM = target output |

**How this differs from Draft 1:**
- self-hosting stays first, but is measured;
- Sema is extracted, not rebuilt;
- handles are used only in the new IR;
- constexpr stays on the tree;
- the allocator is per family;
- target 2 comes second, in stages;
- the encoder is shared with masm.

**How it differs from Fable's alternate:**
- no parallel front end;
- target 2 comes second, not last;
- the old optimizer is deleted within the milestone that replaces it;
- R3.1 is kept.

## 9. Requirement changes proposed

| R | Change |
|---|---|
| R4.3 | "C and C++ share one parser with a dialect parameter; C++'s semantic rules derive from C's." |
| R4.5 | "Constant expressions are evaluated in Sema on the typed tree, with the target's layout, rejecting undefined behaviour. A bytecode VM runs programs without linking and is a test reference." |
| R5.3 | "...replaced in the source" becomes: "provided by the library: `<unordered_map>`, `<thread>`, `<mutex>`, `<atomic>`, `<chrono>`, `<cerrno>`, `std::function`; `<regex>` and `<deque>` leave the source." |
| R6.3 | Scope it: "The IR and machine IR use index handles; the AST and types satisfy R6.1-R6.2 through `unique_ptr`, containers and interning." |
| R1.2 | Name the measures: conformance count, self-compile time, sim6747 cycles, x86-64 benchmark time, `--health` findings. |
| R9.2 | Name where the 14 probes live. Neither the review nor I could find them. |
| R3.1, R9.1, R2.1 | **Unchanged.** Fable proposes changes; section 1.2 gives the reasons to keep them. |
