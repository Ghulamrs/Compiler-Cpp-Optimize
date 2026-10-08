# Critical review: C++Optimize (`cpp11` 1.6, seal 2891841F) and its place in RIDE (tree labelled 4.7, actually at 5.1)

**Consolidated report, second pass.** Reviewer stance: independent, read-only. Each claim is marked **[ran]** (executed on the Cowork VM — an aarch64 Linux sandbox on your Mac — with scratch under `$HOME/review-scratch`) or **[read]** (inferred from source/docs). Nothing in any mounted folder was modified. The second pass had read access to the whole `Developer/Claude` tree (ASM6x, LNK6x, MASM, LINK, SIM6747, RTS6x, VM6747).

---

## 1. Executive verdict

1. **cpp11 is a real, self-written compiler, not a wrapper.** It builds clean from `src/` with g++ 11 `-Wall -Wextra -Werror -pedantic` **[ran]**; its x86_64-linux assembly for ~70 probe programs and a 16-file/14k-line project assembles with clang and links with lld **[ran]**; its arm64-darwin output, transliterated to ELF, **runs correctly on real AArch64 hardware** for ~60 probes including lambdas, virtual/multiple/virtual inheritance, RTTI, member pointers, templates, and nested exceptions/rethrow/cleanups **[ran]**; its tms6747 output goes through the project's own `asm6x` for 397 of 397 suite cases **[ran]**.
2. **"ISO C++11" is not an honest headline.** Of 122 feature probes, 56 were refused. Missing or broken are headline C++11 features: `enum class`, `override`/`final`, `=default`/`=delete`, delegating and inheriting constructors, alias declarations/templates, inline namespaces, `thread_local`, raw strings, UDLs, trailing return types, `extern template`, **forwarding references** (`template<class T> f(T&&)` cannot be called with an lvalue), uniform/list initialisation in expressions, `std::nullptr_t`, lambda→function-pointer, default template args on function templates, `constexpr` constructors, and essentially all of the C++11 library (`<memory>`, `<functional>`, `<tuple>`, `<array>`, `<unordered_*>`, `<thread>`, `<atomic>`, `<chrono>`). Honest estimate: **roughly half of the C++11 language delta over C++03 and under a fifth of the C++11 library; the solid core is C++98/03 plus `auto`/`decltype`/lambdas/rvalue-refs/variadic type packs/`static_assert`/`constexpr` functions/`nullptr`/range-for.** Your own flagship demo (`RIDE-4.7/projects/compilerpp`, `AST.h:45`) is explicitly "C++98 only, everywhere. No `override` keyword exists".
3. **`docs/EXCLUSIONS.md` — the document your rule 5 depends on — fails its own oracle:** `tools/exclusions --check` reports **131 sites, 116 uncited, 112 stale citations** **[ran]**. It still lists `typeid` and overloaded `operator new` as refused; both compile **[ran]**. README.1ST says version 1.4 sealed 18-09-2026; the binary says 1.6 sealed 08-10-2026.
4. **cpp11 is genuinely the compiler RIDE invokes for C++** (the entity diagram `docs/ride-5.0-architecture.html` and the manual are checked against the code in §7: the diagram is right about the tiers, stale on seals and versions, silent on the Debug→clang path, the cpp11→RTS6x build edge, parallelism and verification; the manual contradicts the code on x86_64-windows assembling and debugging), and `VM6747/Compiler-Cppi` (what RIDE's `workspace.mk`, `RIDE.sln`, `RIDE.xcworkspace` and `build.bat` all build) is **byte-identical** to `C++Optimize` — a git submodule of the same repo pinned at the same commit `2dd633b` **[ran: md5 over src/include/lib/tests/tools/ide/examples identical; seals identical]**. But of the three stated back-end paths, **only one runs by default in the IDE**: the C6000 `Run` (F5) executes the `.s` text on `vm6747`, whose `Runtime.cpp` implements libc **and** `__cxa_throw`/`__cxa_begin_catch` natively — ASM6x, LNK6x and RTS6x never execute; the x86-64 MASM→LINK path is bypassed for every **Debug** build (the IDE's default) in favour of clang + Microsoft `link.exe`; and cpp11's own `linkTi()` links against TI's `rts6740_elf.lib`, never RTS6x.
5. **New in this pass — the MASM path is broken and untested.** cpp11's `-masm=masm` dialect is refused by the project's own MASM for **10 of 379** suite cases (duplicate `$LNleave…$catch$0` labels in every function with a `catch`; `.data SEGMENT … COMDAT` for template/static data members) **[ran]**, and the Windows suite never exercises that dialect — `run-cases.cmd` goes through clang's GNU spelling unless `CXX1_CASES_FLAGS=-masm=masm` is set by hand.
6. **The sibling tools are better verified than I first credited**: ASM6x, LNK6x, MASM, LINK and SIM6747 each hold themselves to recorded TI/Microsoft artefacts (objects, images, byte tables, traces) and all five build and pass their beds here **[ran]**. The remaining weakness is structural: the *gates* (`tms6747.sh`, RTS6x `make check`, RIDE's Run) use only your own tools, and the TI-simulator runs are manual campaigns — your 06-10 campaign found 26 EH failures and a wrong DPSP delay count that the self-made chain had hidden.
7. **Parallelism is real but shallow**: a per-file thread pool inside cpp11 (`-j`), deterministic across `-j` **[ran, 4 targets, byte-identical]**; stages inside a file are sequential in-memory hand-offs; RIDE adds no parallelism and serialises C6000 per-file compiles with `&&`.
8. **No fabricated outputs, no silent fallback to cl/gcc/ml64/cl6x/lnk6x** in cpp11 or in any of the seven sibling tools (zero `system()`/`popen()`/spawn sites in their `src/`) **[ran: grep]**. The one "use the vendor's tools instead?" fallback in RIDE is prompted, logged, and refused in headless builds.

---

## 2. C++11 feature matrix

All probes compiled with `cpp11 -S -arch x86_64-linux` **[ran]**; "runs" = executed on AArch64 via arm64-darwin→ELF transliteration (variadic calls excluded, Darwin passes them differently).

| Clause / feature | Status | Evidence |
|---|---|---|
| **[lex]** raw strings `R"(...)"` | **Missing** | `src/Lexer.cpp:392` |
| `u8`/`u`/`U`/`L` strings, `u'x'`, `char16_t/char32_t` | Supported | probe `u8` runs |
| UCNs `é` | Supported | probe `ucn` |
| User-defined literals | **Missing** | `ParserType.cpp:1938` |
| `long long`, `ULL` | Supported | runs |
| digit separators / binary literals | Refused as C++14 (correct) | `Lexer.cpp:189,318` |
| **[cpp]** `#`, `##`, `__VA_ARGS__`, `#if` exprs, `#line`, `#error`, `#pragma once`, `<cstdint>` macros | Supported | probes `pp1`,`pp2` (`__COUNTER__` absent — extension) |
| **[dcl]** `auto` (objects, `auto*`, `const auto&`) | Supported | runs |
| `auto` from braced list | Missing | `ParserTemplate.cpp:1061` |
| `decltype` (expr, member, parenthesised) | Supported | `decltype2`, `decltypeexpr` run |
| `decltype(auto)` | C++14, refused (correct) | |
| `constexpr` functions (recursive), variables, double folding | Supported | `constexprrec` runs; `static_assert(fact(5)==120)` |
| `constexpr` constructors / literal classes | **Missing** | `ParserType.cpp:577` |
| `static_assert` (with message; dependent) | Supported | |
| `alignas` on class/global, `alignof` | Supported | `alignas` runs |
| `alignas` on a local | Missing | `Parser.cpp:744` |
| `enum class` / scoped enums | **Missing** | `ParserType.cpp:1374`; `enum E : short` works |
| alias declaration `using X = T;` / alias templates | **Missing** | `ParserConst.cpp:16`, `ParserTemplate.cpp:365` |
| inline namespaces | **Missing** | `ParserType.cpp:1595` |
| namespace alias | Missing (C++98) | `ParserTopLevel.cpp:79` |
| attributes | only `[[noreturn]]`,`[[carries_dependency]]` parsed; any other attribute is a **hard error** (should be ignored per [dcl.attr]) | `ParserType.cpp:1561` |
| `noexcept` spec + operator | Supported | `noexcept1` runs |
| `throw(T)` dynamic spec | Missing (deprecated, acceptable) | `ParserConst.cpp:87` |
| `override` / `final` | **Missing — hard error on the keyword** | `ParserType.cpp:1002-1007`; any modern header fails to parse |
| `= default` / `= delete` | **Missing — hard error** | `ParserConst.cpp:26` |
| delegating constructors | **Missing** | `ParserTopLevel.cpp:1027` |
| inheriting constructors / using-declarations in classes | **Missing** | `ParserType.cpp:553` |
| NSDMI `int a = 1;` | Supported; `int b{2}` refused | `ParserType.cpp:1114` |
| `thread_local` | **Missing** | `pending[]`, `Parser.cpp:96` |
| trailing return type | **Missing** | `ParserTopLevel.cpp:578` |
| `friend class X` | Missing | `ParserType.cpp:672`; one named friend function works |
| const member in mem-init list | **Missing (C++98 basic)** | probe `constmember` |
| anonymous unions | Missing (C++98) | `ParserType.cpp:763` |
| **[expr]** lambdas: `[a,&b]`, `[=]`, `[&]`, `[this]`, `->T`, deduced return | Supported | `lambda1`,`lambdathis`,`lambdaret` run |
| lambda → function pointer | Missing | `ParserOverload.cpp:1060` |
| init-capture | C++14, refused (correct) | |
| rvalue refs, move ctor/assign, `std::move` | Supported | `rvalue2`, `moveassign`, `stdmove2` run |
| forwarding references / reference collapsing / `std::forward` | **Claimed-but-broken** | `template<class T> int f(T&&)` → `f(i)` with lvalue: "no function called 'f' takes these 1 argument(s)"; `f(1)` works. Perfect forwarding impossible. |
| list-init `P p{1,2}`, `T{...}`, `new T{...}`, braced call args | **Missing** (only `= {..}` aggregates, `{}`, `initializer_list` ctor in declaration form) | `ParserInit.cpp:899`, `ParserExpr.cpp:470,1398` |
| `std::initializer_list` ctor | Partial | `V v{1,2,3}` runs; `sum({1,2,3})` fails |
| `nullptr` | Supported; `std::nullptr_t` undeclared | `<cstddef>` |
| range-for over arrays / pointer-iterator containers | Supported | runs |
| range-for over class iterators / free `begin()` / temporaries / braced lists | Missing | `ParserStmt.cpp:467-562` |
| `sizeof...`, variadic **type** packs, recursive expansion | Supported | `variadic`, `sizeofpack` run |
| non-type parameter packs | Missing | `ParserTemplate.cpp:63` |
| explicit conversion operators | Supported | `ctorexp` runs |
| `static_cast<D&>(base_ref)` | Missing | `ParserExpr.cpp:276` |
| **[temp]** partial specialisation, SFINAE, `>>`, default class-template args, member fn templates (explicit args) | Supported | run |
| default template args on **function** templates | **Missing** | probe `deftmplarg2` |
| explicit specialisation of function templates | Missing | `ParserTemplate.cpp:527` |
| template template parameters | Missing | `ParserTemplate.cpp:43` |
| `extern template` | Missing | `ParserType.cpp:1575` |
| out-of-line ctor of class template | Missing | `ParserTemplate.cpp:396` |
| two-phase lookup | Not implemented (token replay) | README, CONFORMANCE.md |
| **[class]** multiple inheritance, covariant returns, abstract classes, virtual dtors | Supported | `multinh2`,`covariant2`,`virtdtor` run |
| virtual inheritance | Supported on Itanium targets; polymorphic vbase refused on x86_64-windows | `virtinh`,`virtdiamond` run |
| unions with non-trivial members | Supported | `unionnt` runs |
| ref-qualifiers | Missing | `ParserType.cpp:966` |
| pointers to members (data, fn, virtual fn) | Supported | `ptrmem`,`ptrmemvirt` run |
| **[except]** throw/catch by value/ref/base, catch-all, rethrow, nested try, cleanups, `noexcept` terminate | Supported on Itanium targets | `excnested`,`excdtor`,`throwptr`,`exccatchall`,`rethrow` run |
| same on x86_64-windows | Partial: class-typed throw, rethrow, try-in-catch refused; and the MASM dialect of every handler is refused by the project's MASM (§4 D15) | `ParserStmt.cpp:1132,1674` |
| same on tms6747 | Claimed; 06-10 TI-simulator run: 26 EH cases failing with RIDE's lnk6x (D1 there), 362/365 with TI's linker | `docs/ride50-test-2026-10-06/REPORT.md` |
| `goto` out of a handler, function-try-block | Missing | `ParserStmt.cpp:2042`, `ParserTopLevel.cpp:1165` |
| **RTTI** `dynamic_cast` (ptr, void*), `typeid`, `<typeinfo>` | Supported (docs say refused — stale) | `rtti2` runs |
| `dynamic_cast` to reference / multi-base | Missing | `ParserExprNew.cpp:644,704` |
| overloaded / placement `operator new` | Supported (docs say refused — stale) | `overloadnew`,`newplace` run |
| **Library** `<string> <vector> <map> <set> <algorithm> <sstream> <iostream> <iomanip> <stdexcept> <type_traits> <limits> <initializer_list> <utility>` | Partial, subset by design | `map`/`set` are sorted vectors, `sort` O(n²), `vector` leaks non-POD on erase, `at()` unchecked, `setfill` silently ignored (`include/iomanip:26`), no `emplace_back`, no `to_string/stoi` |
| `<memory> <functional> <tuple> <array> <unordered_map> <thread> <atomic> <chrono> <regex> <random> <cstdarg>` | **Missing** | "cannot find" **[ran]**; `<stdarg.h>` works |

---

## 3. Toolchain integration

### 3.0 Which cpp11 RIDE builds — verified
- `RIDE-4.7/workspace.mk:28` `CXX1_DIR ?= ../VM6747/Compiler-Cppi`; `RIDE.sln:7` → `..\VM6747\Compiler-Cppi\ide\cxx1.vcxproj`; `RIDE.xcworkspace/contents.xcworkspacedata:14` → `../VM6747/Compiler-Cppi/ide/cxx1.xcodeproj`; `build.bat:194`; `tools/master-seal:26`. **[read]**
- `VM6747/.gitmodules` pins `Compiler-Cppi` to `git@github.com:Ghulamrs/Compiler-Cpp-Optimize.git`, branch main; `git ls-tree HEAD` in VM6747 shows commit `2dd633b…`, the same HEAD as `C++Optimize`. Working trees: `diff -rq` finds only `.claude/`, `good-bye.txt`, `nul`, Xcode user-data; md5 over every source/test/tool file is identical; `cxx1-1.6.dat` identical. **[ran]** → no divergence today.
- **Risk:** `packaging/release.sh:8,44-54` deliberately takes Compiler-Cppi "at the head of its own default branch, **not at the commit pinned**", and only *says* when they differ. A release can therefore ship a cpp11 that VM6747's pin (and its c90/emulator tests) never saw. (D16 below.)

### 3.1 cpp11 is central
RIDE routes `.cpp` to `ToolCxx1` by default (`toolchain.cpp:190-196`), finds `cpp11.exe` beside itself (`editor.cpp:273-279`), and the C6000 side depends on it twice over: **RTS6x is built by cpp11 + asm6x + ar6x** (`RTS6x/Makefile:1,14-15,76-104`: `$(CPP11) -arch tms6747 … -S` then `$(ASM6X)`, packed by `tools/ar6x`), and so is the Shalimar C6000 runtime (`workspace.mk:77-82,125-131`). The Target menu drives `arch_` → `-arch` on every recipe (`editor.cpp:2349 chooseArch`, `2648/2725/2898`); your audit's F1 is fixed. **[read]**

### 3.2 Path (a): x86-64 MASM → LINK (Windows PE)
- cpp11: `-masm=masm` emits MASM with a `SEGMENT … COMDAT(sym)` extension (`src/backend/Masm.cpp`); `-masm=ml64` is the COMDAT-less dialect **[ran, diffed]**. Default is `masm` only when `masm.exe` is beside `cpp11.exe` (`Driver.cpp:489-500`); assemble/link at `Driver.cpp:630-648, 758-800` (`masm.exe /nologo /c /Fo`, `link.exe /subsystem:console /stack:8388608 … libcmt libcpmt libucrt libvcruntime kernel32 legacy_stdio_definitions`). `CPP11_AS`/`CPP11_LD` override.
- MASM (the project's) accepts the dialect for **369 of 379** suite cases and 16/16 compilerpp files **[ran, with a MASM built from `Claude/MASM/src`]**. The 10 refusals: `handler-exit-ms, lambda-return-through-try, local-try-states, throw-class, throw-pointer, throw-pointer-base-adjust, try-catch` (every function with a `catch`: `'$LNleave$<fn>$catch$0' is already defined` — cpp11 emits the label twice under `OPTION NOSCOPED`), and `static-member-init-scope, static-reference-member, template-static-member-on-use` plus my probe `tmplstatic` (`.data SEGMENT ALIGN(16) 'DATA' COMDAT(...)`: "unexpected text after .DATA" — `.data` is a directive, the segment needs a MASM name such as `_DATA`).
- MASM's own bed: 22/22 instruction files byte-identical to ml64 14.44, `tests/run.sh` all pass **[ran]**. LINK's bed: 19 probes vs `link.exe` images, 10 byte-identical, 9 within pinned known-differ counts (import-table order, Rich header), `bad.sh` passes **[ran]**. Neither bed contains a cpp11-generated C++ object with EH tables or COMDAT data; LINK's `tests/corpus.sh` runs only on the Windows box.
- RIDE: `assemblerFlag()` `toolchain.cpp:941-948` adds `-masm=masm` only when settings name an assembler; `prepareFor()` `:900-920` exports `CPP11_AS/CPP11_LD`. **Every Debug build forces `-masm=gnu` and Microsoft `link.exe /DEBUG`** (`debugsWithCodeView` `:397`; `linkRecipe` `:707-716`). The editor defaults to Debug. So the project's MASM/LINK is exercised only in Release, only on the Windows box, and the test suite there uses clang (`tools/windows/run-cases.cmd:20-22`).

### 3.3 Path (b): TI C6000 — cpp11 → ASM6x → LNK6x
- cpp11 emits TI syntax with EABI unwind tables (`.c6xabi.exidx`, `__c6xabi_extab$`, `__c6xabi_unwind_cpp_pr2/3`, `$EXIDX_FUNC`), compact instructions by default, `__c6xabi_div*/fix*/flt*` runtime calls (`src/backend/Tms6747.cpp:802-862, 2277-2308`). A4/B4 args, A4 return, B15 stack. `-c` runs `asm6x` (beside, `$CPP11_AS`, PATH) `Driver.cpp:502-510, 630-640`; `linkTi()` `:704-756` writes a flat `--rom_model` map (`:653-680`) and runs `lnk6x -mv6740 --abi=eabi … -l rts6740_elf[_eh].lib`.
- **Driver→asm6x verified here**: `tests/asm6x.sh` with a locally built asm6x: "397 objects through the driver as by hand, 0 failed, 7 not for this target" **[ran]**.
- ASM6x's own bed vs TI asm6x 8.2.2 goldens: `tests/run.sh` 10 files, 10 table-identical (3 of 7 byte-identical), review probes "317 identical, 117 refused by both, 11 known, 0 disagree", link probes 11/11; `tests/compact/check.sh` 14 files, **13 identical, 1 differs (`c13-packets`) — undocumented** **[ran]**.
- LNK6x's bed vs TI lnk6x images: 54 matched, 22 within pinned known-differ (the six runtime probes differ by ~31 KB each in the unwind index, attributes blob and symbol table — "nothing that runs"), `bad.sh` passes **[ran]**. Note the 06-10 campaign: RIDE 5.0's shipped LNK6x "misplaces C++ unwind tables" (D1 there) — a defect the bed's pinned differences in exactly that region did not catch.
- RIDE: `targetRecipe/objectRecipe` for `tms6747` issue **one `cpp11 -S` per source chained with `&&`** (`toolchain.cpp:607-616, 660-671`) into `<program>.vm/`; `makeTiProgram()` (`compile.cpp:608-760`) runs asm6x on every `.s` and lnk6x with `tiLinkCmd()` (`:582-598`).
- **Default Run (F5) for tms6747 is `vm6747 <file>.s`** (`toolchain.cpp:773-781, 326-343`). `VM6747/Emulator/src/Runtime.cpp` implements `printf`, `puts`, `fopen`, `malloc`, `memcpy`, `strlen`, …, **and `__cxa_throw` / `__cxa_begin_catch`** natively (`:645-669, 1020-1239`) **[read]** — no asm6x encoding, no lnk6x, no RTS6x, no TI boot.
- SIM6747 (`--run prog.out`) is the machine-code path, held to CCS 5.5 traces in `oracle/` (`compare-trace.py`, `compare-dis.py`); `FIX-REQUEST.md` records five fatal defects found on 05-10 by that oracle (store data read in E3, compact `MVC ILC`, base-update timing, unwinder null context, SPLOOP epilog).

### 3.4 Path (c): RTS6x
- **Confirmed: built from cpp11 output.** `RTS6x/Makefile` compiles every `.cpp` under `src/{boot,ctype,cxx,eh,exit,host,locale,math,misc,stdio,stdlib,string,time}` with `cpp11 -arch tms6747 -O2 -DNDEBUG=1` (release) and `-O0 -D_DEBUG=1` (debug), assembles with asm6x, packs with its own `ar6x`; `boot.s` provides `_c_int00`, `C$$EXIT`, `__TI_CINIT_Base/Limit`, `__TI_STACK_END` **[ran: `readelf` on `rts6x.lib` members; Makefile read]**. `tools/provenance` enforces a "Spec:" line per file and no foreign licence marks.
- **`rts6xd.lib`, `printf6xd.lib` are producible** (`Makefile:22,47,105-112`, `make check` runs the suite against both); **`shmrt6x.lib`/`shmrt6xd.lib` are produced by `workspace.mk:125-131`** from the Shalimar runtime's `.s`. In the checkouts as mounted only `build/rts6x.lib` and `printf6x.lib` exist, and `RIDE-4.7/bin/lib/rts6x-tms6747/` lacks `rts6xd.lib` and both `shmrt6x*.lib` — a Debug C6000 link from that `bin/` fails; `make confirm` (`RIDE Makefile:252-271`) would catch it.
- Verification: `make check` runs on **vm6747 and sim6747 only** (`tests/run.sh:1-14`); the independent oracle is `tools/referee`, a manual campaign that runs both suites' images on TI's CCS 5.5 simulator on the Windows box (`tools/referee:1-25`). README claims printf parity with the host "on sim6747 and on TI's CCS 5.5 simulator".
- ABI: ELF/EABI, `--rom_model`, consistent with cpp11's `--abi=eabi` output. **Only RIDE links against it** (`compile.cpp:659-708`); cpp11 standalone never does (`Driver.cpp:734`; zero `rts6x` in `src/`).
- Seal drift: `RTS6x/rts6x-1.2.dat` exists; `RIDE-4.7/MASTER.SEAL` references `rts6x-1.1.dat`.

---

## 4. Dodge-condition findings (ranked)

| # | Sev | Finding | Evidence / repro |
|---|---|---|---|
| D1 | **Medium-High** (was High; partly disclosed — §8) | **The IDE's default C6000 path is not the stated pipeline.** F5 runs cpp11's *assembly text* on vm6747, whose `Runtime.cpp` natively implements libc and the C++ EH runtime; asm6x, lnk6x, RTS6x and TI boot never execute. `help/06-the-project.md:98,123` and the architecture page do say "the emulator runs the assembly" / "vm6747 runs the .s"; neither says the emulator carries its own libc and EH runtime, nor that a program "run" this way has never met asm6x's encoding, lnk6x or RTS6x. Your own 05/06-10 reports record this hiding 26 EH failures and a DPSP delay-slot error shared by compiler and emulator. | `toolchain.cpp:773-781`; `VM6747/Emulator/src/Runtime.cpp:645-669,1210-1239`; `O2-REPORT-2026-10-04.md` T4 |
| D15 | **High (new)** | **cpp11's `-masm=masm` dialect is refused by the project's own MASM for 10/379 cases** (every function with a `catch`, and static/template data members), and **the Windows suite never runs that dialect** (clang unless `CXX1_CASES_FLAGS=-masm=masm`). The MASM→LINK path exists but is not held to the test suite; LINK's bed has no cpp11 object in it. | `cpp11 -S -arch x86_64-windows -masm=masm tests/cases/try-catch.cpp` → `masm.exe /c` → "`$LNleave$?inner@@YAHH@Z$catch$0` is already defined" **[ran]**; `tools/windows/run-cases.cmd:20-22` |
| D2 | **High** | **`makeTiProgram` returns with `result.ok == true` when asm6x is absent, or when no runtime/linker is available** — the build is reported successful, no `.out` exists. | `compile.cpp:614` (`if (as.empty()) return;`), `:665-668` |
| D3 | **High (confirmed; docs contradict code)** | **The MASM→LINK path is bypassed for every Debug (default) build**, replaced by clang + Microsoft link.exe. Documented only in a code comment; the user manual says the opposite — `help/07-building.md:53` "x86_64-windows: MASM, assembled by ml64", `help/cpp.md:72` "on x86_64-windows it writes MASM and no line table", `help/08-debugging.md:43` "no — MASM carries no line table" — and the architecture page draws only masm/ml64 and link/link.exe. | `toolchain.cpp:397, 905-918, 941-944, 710-713` |
| D4 | **High** | **`docs/EXCLUSIONS.md` fails its own oracle**: 116/131 sites uncited, 112 stale citations; still lists `typeid` and `operator new` overloads as refused. Counts disagree three ways (CLAUDE.md 101, EXCLUSIONS 130, tool 131). | `python3 tools/exclusions --check docs/EXCLUSIONS.md` **[ran]** |
| D5 | Medium | **Prompted native fallback**: on failure of the project's masm/LINK/lnk6x RIDE offers Visual Studio's / TI's tools and retries with `settings::forceNative(true)`; the console says only "building again with the native tools" and nothing marks the resulting binary. Refused headless. | `compile.cpp:361-437` |
| D6 | Medium (nuanced) | **Day-to-day gates are self-referential**: `tests/tms6747.sh` runs on vm6747 **and** sim6747 against RTS6x linked by LNK6x; RTS6x `make check` the same; `tests/asm6x.sh` compares asm6x with asm6x. **Mitigation found this pass:** each tool keeps recorded TI/Microsoft artefacts (ASM6x objects, LNK6x images, MASM bytes, LINK images, SIM6747 traces) and passes them here. But those beds are small (7–25 probes each, ~30 KB "known" differences in LNK6x's runtime probes) and the real-hardware-equivalent runs (`c6747-three`, `o2run`, `referee`) are manual. | script headers; `LNK6x/tests/known-differ.txt` |
| D7 | Medium | **Self-generated golden**: `tests/emit.sh` diffs against `out-emit.golden` recorded from cpp11 itself and "never fails the run"; counted among "the four suites". | `tests/emit.sh:12-19` |
| D8 | Medium | **Documentation drift presented as release facts**: README.1ST "1.4 sealed 18-09-2026", `-masm` options (no `ml64`), "-g linux/darwin only", "Known shortcomings in 1.4" lists `typeid`/placement-new as refused; `-version` prints "PST" under `TZ=Asia/Karachi`. | `cpp11.exe -version` **[ran]** |
| D9 | Medium | **cpp11 and RIDE link C6000 programs against different runtimes** (TI rts6740 vs RTS6x) with nothing distinguishing them; RTS6x unreachable from the command line. | `Driver.cpp:734` vs `compile.cpp:659-708` |
| D10 | Medium | **Measurement exemptions**: 16 `lambda*` cases "exempt at the user's request" from the 04-10 TI exercise; 76 cases have no TI build so clang's output "stands in"; `tests/corpus.sh` always exits 0. | `O2-REPORT-2026-10-04.md` §1, `corpus.sh:7` |
| D16 | Medium (new) | **Releases ignore the submodule pin**: `release.sh` builds Compiler-Cppi from the remote default branch head and only prints a note if it differs from VM6747's pin. Today they coincide; nothing enforces it. | `RIDE-4.7/packaging/release.sh:8,44-54` |
| D11 | Low | 27 empty `.nocl` files silently exclude cases from the cl-name comparison with no reason. | `tests/cases/*.nocl` **[ran]** |
| D12 | Low | **Silent semantic downgrades**: `volatile` dropped and the file compiled at `-O0` with a note (`Driver.cpp:1132`); `setfill` ignored (`include/iomanip:26`); `vector::at()` unchecked; unknown attributes are hard errors. | |
| D13 | Low | **Seal ≠ verification.** CRC32 over sources only; `RIDE-4.7/bin/cpp11.exe` is sealed 03-10 vs MASTER.SEAL 08-10; MASTER.SEAL names `rts6x-1.1.dat` while RTS6x is at 1.2. | `tools/seal`, `strings bin/cpp11.exe` **[ran]** |
| D14 | Low | RIDE's suite skips (reported, not failed) every compiler-driven case when tools are absent — 11 skip blocks in a 1212-check run here. | `tests/test` **[ran: 1212 checks, 5 failed, all host-arch limits]** |
| D17 | Low (new) | ASM6x `tests/compact/check.sh`: `c13-packets` differs from TI's compressed object with no entry in any known-list. | **[ran]** |

**Not found** (looked for explicitly, in cpp11 and all seven sibling tools): canned outputs; calls to cl/gcc/clang/cl6x as a compiler; `#if 0`/TODO/FIXME paths that exit 0; swallowed exit codes in RIDE (`runCaptured` returns the child's status, `compile.cpp:274-305`); stubs in codegen (`unsupported()` → `std::exit(1)` in all backends); any `system()`/`popen()`/spawn in ASM6x, LNK6x, MASM, LINK, RTS6x, SIM6747, VM6747 `src/` (zero sites) **[ran: grep]**.

---

## 5. Parallelism and stage hand-off

**cpp11 [read + ran]:**
- Granularity: **per translation unit**. `Driver::runJobs` (`Driver.cpp:1267-1310`) spins `threadCount()` threads (physical cores via `sched_getaffinity`, only when ≥4 files or `-j n`, `kThreadFrom = 4`) pulling job indices from an `std::atomic`. Assembler runs are pooled the same way (`runCommands` `:1219-1265`). Linking is one serial `runTool`.
- Within a unit, strictly sequential and in memory: `Preprocessor(...).run()` → `Source` text → `Lexer(src).tokenize()` → `std::vector<Token>` → `Parser(...).parse()` → `Program` AST → `backend_->codegen(ostream)` walking the AST (`Walker`), emitting instructions into an `Optimizer` that implements the `Spelling` interface (instruction-stream IR `Mir*`, register allocation, peepholes — no mid-level IR) and writing text to the `.s` (`Driver.cpp:1109-1170`). **C6000 is different: the optimizer is a text→text pass over emitted assembly** (`c6xSchedule(std::string)`, `Tms6747.cpp:2270-2271`, `C6xSched.cpp:2254`); commit `0198522` "liveness knows the text it was computed for" is the kind of bug that design invites.
- Hand-off to assembler/linker: **files on disk**, one temp folder per job named after the source (`temporaryName` `:544-577`), objects beside them, one link. Stage N+1 starts only after `pool.join()`.
- Shared mutable state: `temporaryNames()/temporaryFolders()` statics touched only on the main thread; `lastToolCommand` `thread_local`; `failedAt` under a mutex; no globals in parser/backend; one benign racy `static int` in `C6xPipe.cpp:21`. **Determinism measured**: 16-file project at -O2, `-j1` vs `-j4`, byte-identical on all four targets **[ran]**.
- Weak spot: `Source::fail` calls `std::exit(1)` from whichever worker hit the error (`Source.cpp:131`) while other workers are mid-compile; other units' diagnostics are lost and `exit()` with live threads is technically UB. Not TSAN-verified.

**RIDE [read]:** no parallel build. Host targets: one `cpp11` invocation (cpp11 parallelises internally; RIDE passes `-j` only via Compiler Options, `options.cpp:155`). tms6747: per-file `cpp11 -S … && cpp11 -S …` — serial by construction. The only thread is the program runner in the WinForms bridge (`bridge.cpp:1907,2030`). Hand-off is files in `<program>.vm/` or a temp objects dir, consumed after each `runCaptured`; cancellable (`compile.cpp:178-270`).

**Sibling tools:** ASM6x, MASM, LINK each run "one thread per input file" (their `main.cpp`), no shared mutable state claimed; not stress-tested here.

"Parallel" is **real for multi-file compiles and assembling, nominal elsewhere**.

---

## 6. Stage verification

| Stage | What checks it | Oracle | Gap |
|---|---|---|---|
| Preprocessor / lexer | ~31 `preprocessor-*` cases + every case | clang's expected output | no `-E` diff against clang |
| Parser / sema | 628 cases: 402 `.expected`, 224 `.error`; `overload.sh` (62) | clang output; the refusal text | 224 "passes" are the compiler saying no |
| Codegen x86_64-linux / arm64-darwin | `run.sh` (610 pass on your Mac, 06-10); `names.sh` 390/390 vs clang mangling | clang | `.nonames` on 202 cases (32%) |
| Optimizer (x86/arm64) | `identical.sh` byte-compare vs previous build; `run.sh -O2` on Linux box | self + behaviour | no IR verifier; 8 asserts in 20k lines |
| x86_64-windows (GNU spelling) | Windows box: 565 cases, 237 names vs cl, `winlink` pair-link (3 pairs) | cl, link.exe | — |
| x86_64-windows (**MASM spelling**) | **nothing in the suite**; one manual `CXX1_CASES_FLAGS` run | — | 10/379 refused by MASM **[ran]** |
| MASM assembler | 22 enc files byte-identical to ml64 14.44; corpus on the box | ml64 | no cpp11 EH/COMDAT objects in the bed |
| LINK | 19 probes vs link.exe images (10 exact, 9 pinned) | link.exe | tiny bed; `corpus.sh` box-only; 09-23 showed Shalimar images linked by LINK segfaulting |
| C6000 codegen | `tms6747.sh` 375 pass (vm6747 + sim6747, RTS6x); `asm6x.sh` 397/397 **[ran]** | **self-made chain** | TI simulator only in manual campaigns; 28 failures there on 06-10 (26 = LNK6x) |
| C6000 optimizer (text pass) | `emit.sh --o2` golden; `o2run` on TI sim | self; TI cycles | `CPP11_C6XLIVECHECK` opt-in |
| ASM6x | 10 enc + 14 compact + 330 review probes vs TI asm6x 8.2.2 objects **[ran]** | TI asm6x | 1 compact probe differs, undocumented |
| LNK6x | 76 probes vs TI lnk6x images (54 exact, 22 pinned) **[ran]** | TI lnk6x | pinned differences cover exactly the unwind-index region where the 06-10 defect lived |
| SIM6747 | `tests/c6x-all.sh`, `oracle/` traces from CCS 5.5 | TI simulator traces | memory system not modelled (cycle.Total) |
| VM6747 (emulator) | its own tests; agreement with sim6747/TI on output | — | native libc/EH; cannot see encoding, linking, runtime |
| RTS6x | `make check` on vm6747 + sim6747; `tools/referee` on TI sim (manual) | TI sim (manual) | no recorded referee result in the tree |
| IDE integration | `tests/test` (1212 checks here), `tests/session`, `toolchain-check` (3 C++ programs end-to-end on Windows) | real tool runs | skips when tools absent |

**What I ran, summary:** out-of-tree build of cpp11; 122 probe programs on 4 targets; assemble+link of all x86_64-linux outputs with clang/lld; execution of ~60 arm64 programs on AArch64; `tools/seal check`; `tools/exclusions --check`; `-j` determinism; RIDE core suite (Linux build of `tests/test.cpp`); builds and test beds of ASM6x, LNK6x, MASM, LINK; cpp11 `tests/asm6x.sh` through the real asm6x; 379 suite cases through `-masm=masm` → MASM; tree/seal/submodule comparison of Compiler-Cppi vs C++Optimize.

**Still not verified:** Windows-only paths (ml64/link.exe/cl/cdb, CodeView); run-time results of x86_64-linux/x86_64-windows/tms6747 code (no x86 or C6000 execution here — relied on your recorded verdict files and the 06-10 CSVs); whether every `.expected` came from clang rather than cpp11; the manual TI-simulator campaigns (`c6747-three`, `o2run`, `referee`) themselves.

---

## 7. Documentation and entity diagram vs code

**Inventory of `RIDE-4.7/docs/` (155 files) [ran]:** `ride-5.0-architecture.html` (the entity diagram — an HTML page of boxes, five tiers, a 3×4 compiler×target matrix, a source/installed tree and a seals table), `debug-tab-mockup.html`, `COMMON-BACKEND-STUDY-2026-10-03.md`, `MIXED-PROJECTS.md`, four review/audit `.md` files (4.5 WinForms, 4.5 macOS, 4.7 GUI, 4.7 macOS), `sample.pro`, and `ccs-reference/` (TI CCS 5.5/7.4 project skeletons, 140 files). The user manual is `RIDE-4.7/help/` (16 `.md` + rendered `.html`). No mermaid/dot/svg/pdf diagram exists; the HTML page is the only one.

### 7.1 Entities and edges in the diagram vs the code

| Diagram element | In code? | Verdict |
|---|---|---|
| RIDE `src/compile.cpp` + `src/toolchain.cpp` "choose the compiler, assembler, linker and runtime" | yes | correct |
| c90 / cpp11 / shalimar front ends, four backends each; cpp11 = `VM6747/Compiler-Cppi` "submodule of Compiler-Cpp-Optimize" | yes, byte-identical to C++Optimize | correct (cpp11's backend list omits `CodeView.cpp`/`CodeViewTypes.cpp` and the four target files; cosmetic) |
| x86_64-windows: "Assembler: masm (cpp11 gets `-masm=masm`), or Visual Studio's ml64; Linker: link if named under Tools, else Microsoft's link.exe" | partly | **Wrong on two counts.** (i) Every Debug build uses **clang** (`-masm=gnu`) and Microsoft `link.exe /DEBUG` — neither appears on the page (`toolchain.cpp:397,941-944,710-713`). (ii) In Release with nothing named, cpp11 picks the `masm.exe`/`link.exe` *beside itself* (`Driver.cpp:489-500`), i.e. the project's own, not "Microsoft's link.exe". |
| tms6747: asm6x → lnk6x → `rts6x.lib / rts6xd.lib` → `.out` | yes, in `makeTiProgram` | correct for a project build (F4) and Run-on-Simulator; **not the F5 path** |
| "vm6747: runs the .s" / "sim6747: runs the .out machine code" | yes | correct, and the one place D1 is visible — but nothing says the `.s` run uses vm6747's own libc/EH and skips the whole tier above |
| "The TI C6747 board itself" | **no code** | RIDE has no loader/flasher/board run; the edge exists only in CCS. Decorative. |
| Installed `bin/lib/rts6x-tms6747/`: `rts6x.lib rts6xd.lib printf6x.lib printf6xd.lib shmrt6x.lib shmrt6xd.lib` | intended | the mounted `RIDE-4.7/bin/lib/rts6x-tms6747/` holds only `rts6x.lib`, `printf6x.lib` **[ran]** |
| Seals table: `ride-5.0.dat 35BD9D0E`, `cxx1-1.5.dat D6B8B482`, `lnk6x-1.0.dat`, `sim6747-1.1.dat`, `rts6x-1.0.dat`, master seal `1732D0A4` | stale | `MASTER.SEAL` (08-10) says `ride-5.1.dat 0A46C214`, `cxx1-1.6.dat 2891841F`, `lnk6x-1.1`, `sim6747-1.2`, `rts6x-1.1`, master `49BE94EC`; the page's own banner says "seals as of 08-10-2026" — self-contradictory on the same date |

**Edges the code takes that the diagram does not draw:**
1. **cpp11 → RTS6x.** RTS6x is *compiled by cpp11* and assembled by asm6x (`RTS6x/Makefile:76-104`); the diagram presents RTS6x as a leaf runtime beside the linker. Likewise `shmrt-tms6747` (Shalimar's C6000 runtime) is cpp11's output (`workspace.mk:77-82`). The compiler's correctness therefore gates the runtime it links against — a dependency worth drawing.
2. **cpp11 standalone → TI `lnk6x` + `rts6740_elf[_eh].lib`** (`Driver.cpp:704-756`) — D9. Nowhere in RIDE's docs; cpp11's own README says it.
3. **RIDE → Visual Studio ml64/link.exe or TI lnk6x on a prompted retry** (`compile.cpp:361-437`, D5) — not drawn.
4. **Debug on x86_64-windows → clang → link.exe /DEBUG → cdb** (M10) — not drawn, and contradicted by the manual (D3).
5. **Parallelism**: nothing in the diagram or the manual mentions cpp11's thread pool, `-j`, or that RIDE compiles C6000 sources serially; the only "thread" in RIDE's docs is the program-runner thread (`README.md:624,647`) and the macOS one-thread-per-project rule (`README.md:869`).
6. **Stage verification**: neither the diagram nor the manual says what verifies any tier (clang names, TI goldens, simulator campaigns). The verification story lives only in each repository's README/CLAUDE.md and `C++Optimize/docs/`.

**Edges the diagram draws that the code never takes:** "The TI C6747 board itself" (above); for the IDE, "ml64" as the x86_64-windows assembler (`help/07-building.md:53`) — RIDE never names ml64; cpp11 uses it only under `-masm=ml64` or when neither `masm.exe` beside nor `CPP11_AS` exists.

### 7.2 Does the documentation disclose the findings, or describe intent as fact?

| Finding | Disclosed? | Where / how |
|---|---|---|
| D1 (F5 runs on vm6747, bypassing asm6x/lnk6x/RTS6x) | **Partly.** "RIDE compiles …, assembles with asm6x, and runs the program on vm6747" (`help/06-the-project.md:97-98`) and "The emulator runs the assembly; a file CCS can load … is linked against a run-time library" (`:123-124`); diagram "vm6747 runs the .s". | Not disclosed: that the "run" never touches asm6x's *encoding*, lnk6x, RTS6x or TI boot, and that vm6747 supplies its own libc/EH. The sentence at `:98` even lists asm6x as part of the run, which it is not for F5 (`toolchain.cpp:773-781`). |
| D2 (`ok=true` with no `.out`) | **No.** `help/06:128-131` says builds "end with `[linked <program>.out]`"; nothing says a build without asm6x/RTS6x/TI still reports success. | describes intent as fact |
| D3 (Debug → clang + link.exe) | **No — contradicted.** `help/07-building.md:53`, `help/cpp.md:72-73,82`, `help/08-debugging.md:43` all say MASM/ml64 and "no line table"; `RIDE-4.7/README.md` (5.1, M10) says the opposite. | manual is pre-M10 and wrong |
| D15 (`-masm=masm` not covered by the suite; MASM refuses 10/379) | **No.** Diagram says "cpp11 gets -masm=masm"; `MASM/README.md` and cpp11's `run-cases.cmd:20-22` admit the dialect was "proven" by one manual run. | |
| D9 (cpp11 standalone links rts6740) | **No** in RIDE docs; **yes** in `C++Optimize/README.md` ("objects go to TI's lnk6x against rts6740_elf.lib") and `README.1ST`. | the two READMEs describe two different link lines for one compiler without saying so |
| D5 (native fallback) | **Partly**: `help/06` mentions "the vendor's tools" question only in RIDE `README.md:1452`; not in the manual's building page. | |
| Parallelism model | **No** (RIDE docs); cpp11's `README.1ST` §1 describes the pool honestly ("below four files … one thread"). | |
| Stage verification | **No** (RIDE docs); `C++Optimize/README.md` "What is verified, and where" and `CLAUDE.md` do. | |
| D16 (release ignores submodule pin) | **Yes**, in a comment in `release.sh:8` only. | |

### 7.3 Documentation that contradicts the code or itself (path:line)

- `help/07-building.md:49` "the Target menu names all three" and `help/07:53-56` list three targets; `help/README.md:33-34` "three targets"; `help/c.md:22`; `help/01-what-it-is.md:92` — the code (`compile.cpp:21`, `menu.cpp:153`) and `help/08-debugging.md:66` ("all four targets") have four.
- `help/07-building.md:53` "x86_64-windows: MASM, assembled by ml64" vs `toolchain.cpp:941-944` (clang in Debug; project masm in Release) and `Driver.cpp:489-500`.
- `help/cpp.md:70-73,82` "cpp11 writes DWARF for x86_64-linux and arm64-darwin … on x86_64-windows it writes MASM and no line table" vs `toolchain.cpp:384-399` (CodeView on x86_64-windows), `C++Optimize` commit `0119712` (M10 W1).
- `help/01-what-it-is.md:110-111` "It has no optimiser of its own. What optimisation you get is whatever the compiler you chose does" — true, but `help/07:78-84` then describes cpp11's -O1/-O2 as a feature; and `help/01` never mentions tms6747, vm6747, sim6747, asm6x, lnk6x or RTS6x at all while the diagram calls them half the product.
- `docs/ride-5.0-architecture.html` seals table vs `MASTER.SEAL` (above); page title "RIDE 5.0" vs `src/about.cpp:28` "5.1" vs folder name "RIDE-4.7"; page says "ride-5.0.dat", tree holds `ride-4.5/4.51/4.7/5.1.dat` and no 5.0.
- `help/06-the-project.md:125-127` promises `rts6xd.lib`, `printf6xd.lib` in the install; the dev `bin/` lacks them and `RIDE/Makefile:252-254` is the only guard.
- `C++Optimize/README.1ST` §6 "version 1.4, sealed 18-09-2026" and §1 "-masm=<m> masm (ml64, the default) or gnu" vs `Version.h` (1.6, 08-10-2026) and `cpp11 --help` (masm/gnu/ml64); `README.md` "Known shortcomings in 1.4" lists `typeid`, overloaded `operator new`, placement new as refused — all compile **[ran]**; `CLAUDE.md` "101 refusal sites" vs `EXCLUSIONS.md` "130" vs `tools/exclusions` "131".
- `README.1ST` §3 "The language is ISO C++11, and a large part of it" vs §2 of this review.

### 7.4 Severity re-assessment after reading the docs

- **D1 High → Medium-High.** The manual and the diagram do state that F5/Run executes the assembly on the emulator and that a `.out` is a separate product; what remains hidden is the consequence (no runtime, assembler or linker of the real chain is exercised, and the emulator's own libc/EH hides defects your own reports found). A one-sentence disclosure plus a default change would close it.
- **D3 stays High and gains "docs contradict code"**: three manual pages assert MASM/ml64 and "no line table" where the code runs clang + link.exe /DEBUG + cdb.
- **D2, D9, D15 unchanged** (undisclosed). **D13 gains** the stale diagram seals table. **D8** (documentation drift) is now better characterised: the manual (`help/`) is pre-M10/pre-5.1 on every toolchain fact that changed, while `RIDE-4.7/README.md` is current — two documents of one product disagreeing.
- Nothing in the docs lowers D2, D4, D5–D7, D9–D12, D14–D17.

---

## 8. Recommendations (priority order)

1. **Fix the headline**: call it "a C++03 compiler with C++11 extensions" until `enum class`, `override/final`, `=default/=delete`, delegating/inheriting ctors, alias declarations, forwarding references and list-initialisation land. These block essentially every real C++11 codebase and header.
2. **Put the MASM dialect under the suite**: make `run-cases.cmd` run every case twice (GNU and `-masm=masm` with the project's MASM + LINK); fix the duplicate `$LNleave…$catch$N` label and the `.data SEGMENT … COMDAT` spelling in `Masm.cpp`; add cpp11-made EH/COMDAT objects to LINK's bed.
3. Regenerate `docs/EXCLUSIONS.md` from `tools/exclusions` and make `--check` a gate in `verify-three`; generate README/README.1ST version lines from `Version.h`.
4. Make `makeTiProgram` failure states fail the build (or a distinct "partial" state the UI shows); never print `[built …]` when no `.out` exists.
5. Make the IDE's tms6747 **Run** execute the linked `.out` on sim6747 by default, with vm6747 as an explicit "quick emulate" — so ASM6x→LNK6x→RTS6x is what users exercise and what the suite gates on.
6. Give cpp11 an RTS6x option (or auto-detect `lib/rts6x-tms6747` beside it) so command line and RIDE link the same runtime; stamp the runtime name into the `.out` build log.
7. Schedule the TI-simulator legs (`o2run` with RTS6x, RTS6x `referee`, LNK6x/LINK corpus) as part of `verify-three`, and record their results in the tree; shrink LNK6x's pinned "known-differ" byte counts by making the unwind index, attributes and symbol table match.
8. Implement forwarding references (deduce `T` as `U&` for an lvalue argument in `ParserTemplate.cpp`'s deduction) — without it `std::forward`, `emplace_back` and generic code are dead.
9. Move the C6000 optimizer onto the `Mir` instruction stream the other targets use, or at minimum run with `CPP11_C6XLIVECHECK` in the suite; document the one ASM6x compact probe that differs.
10. Bind seals to artefacts and pins: SHA-256 of each shipped `*.exe`/`*.lib` in MASTER.SEAL; `release.sh` refusing when Compiler-Cppi's branch head differs from VM6747's pin, or when `bin/` lacks `rts6xd.lib`/`shmrt6x*.lib`.
