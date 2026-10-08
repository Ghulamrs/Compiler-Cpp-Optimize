# Plan for answering the critical review of 2026-10-08

**Written 2026-10-08 against cpp11 main `2dd633b` (1.6, `cxx1-1.6.dat` 2891841F),
RIDE main `644d6d7` (5.1, MASTER.SEAL DC1EF907), RTS6x `500d92d` (1.2), ASM6x
`b454787`, LNK6x `c8d3d9e` (1.1), MASM `d606874`, LINK `a2ff14b`, SIM6747 `4cea3f8`
(1.2), VM6747 `b9d96ab` (c90 1.2).** The review is
`CppOptimize-critical-review_1.md` at the root of this tree (273 lines); every
finding in it is in the register below with a disposition, and nothing is left
untouched. Planning only: no source in any tree was modified to write this.

The plan finishes inside 48 wall-clock hours with at most six workstreams running
at once. Each workstream is one Fable 5.1 agent in its own git worktree and
branch, owning a named set of files; two items that must touch one file are in
one workstream, or sequenced and said so.

---

## 0. Standing rules - every executor reads these first

1. **Clean room.** Nothing is copied from GCC, LLVM, MSVC, TI, libc++, libstdc++
   or any other implementation - not code, not tables, not test files. Measure
   the oracles (`clang++ -x c++ -std=c++11 -pedantic-errors` for the language and
   for every `.expected`; `cl` for the Microsoft ABI; TI's own assembler, linker
   and CCS 5.5 simulator for tms6747) and write our own.
2. **Comments in `src/` are at most three lines per group; a comment in front of
   a single line of code is one line.** `make comments` must read 0 before a
   branch is handed over. The story goes into the workstream's handover note,
   not into the code.
3. **The Mac is a control room.** Edit, use git and the seal tools on the Mac.
   Every build and every test runs on the Windows PC (`ssh windows`, `cmd.exe`;
   tool workspace `C:\cxx1\rtsdiv\<workstream>`; clones with
   `core.autocrlf=false`) or on the Linux box (`ssh compiler-box`, `~/ride-5.1`,
   g++/gcc, no clang). `names.sh` and `overload.sh` need clang: on the Windows
   box they run under Git's bash (`C:\Program Files\Git\bin\bash.exe`, which
   `tools/crossbox` and `tools/c6747/o2run.py` already use) with Visual Studio's
   LLVM first on PATH - `C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\x64\bin`
   - and `CLANG` pointing at its `clang++.exe` for `overload.sh`
   (`tests/names.sh:11` asks `command -v clang++`, `tools/mangled-names:145`
   calls `clang++` by name). WS-A's first hour confirms `clang++.exe` is in that
   directory and otherwise adds `tools/windows/clang++.cmd` as a shim; every
   later gate uses `tools/windows/names-overload.cmd`, which WS-A writes
   (section 4).
4. **Never run a TI-simulator job (CCS/DSS, `c6747-three`, `c6747-levels`,
   `release-check`, `referee`, `o2run`) except as a scheduled step named in this
   plan, one at a time on the box, under the 30-minute cap.** Only WS-H and the
   final gate run them.
5. **No test case puts two calls with side effects in one argument list**
   (`printf("%d %d", f(), g())`): evaluation order is unspecified and the
   Windows box has already caught it twice. One call per statement.
6. **Every language `.expected` is clang's output** under
   `-std=c++11 -pedantic-errors`, produced on the Windows box by VS clang, never
   cpp11's own. Every `.error` case names the clang column. A new feature also
   agrees with clang in `names.sh` or carries a `.nonames` with the measured
   reason on its line.
7. **The emit golden is recorded before the change** (`tests/emit.sh --record`
   on the box, in the workstream's own workspace) **and every changed file is
   read**, with the count and the kind of change in the handover note.
8. **A refusal names the feature** ("... is not supported yet", or the standard
   version it belongs to), `docs/EXCLUSIONS.md` cites it, and
   `tools/exclusions --check docs/EXCLUSIONS.md` reads 0 uncited, 0 stale on
   the branch. A landed feature deletes its refusal and its `.error` case in the
   same commit.
9. **Executors commit on their own branch (`review/<ws>`), never push, never
   merge.** The handover is a note `docs/review-2026-10-08/<ws>.md` in the
   branch (what was measured, what moved in the golden, what is left), from
   which the main session writes the CLAUDE.md section and merges after the
   gates of section 6.
10. Branch from `main` as it stands when the workstream starts; a workstream
    marked *after X* in section 5 branches from X's merged tip.

---

## 1. Item register, verification and disposition

Column **Today** says what the trees show on 2026-10-08: *true* (as the review
says), *partly* (what differs), or *fixed* (by which commit or file). Column
**Disp.** is one of IMPLEMENT, FIX, DOCUMENT, REFUSE-BY-NAME, DECLINE. Column
**WS** names the workstream of section 4.

### 1.1 Section 1 - executive verdict

| ID | Finding | Today | Disp. | WS |
| --- | --- | --- | --- | --- |
| E1 | cpp11 is a real self-written compiler; builds clean, assembles, runs | true | DOCUMENT - the "What is verified" paragraph of README.md gains the review's measurements as a dated line | A |
| E2 | "ISO C++11" is not an honest headline; ~half the language delta, under a fifth of the library | true: `Version.h` banner "ISO C++ 11", README.1ST §3 "ISO C++11, and a large part of it", EXCLUSIONS "C++11 minus this list" | FIX (headline) + IMPLEMENT (Rec 1 list). Until the Rec-1 features land the banner, README.md, README.1ST §3, EXCLUSIONS.md opening and CLAUDE.md's first section read "C++03 with C++11 extensions - the list is docs/EXCLUSIONS.md"; the final step of this plan re-measures the 122-probe grid and rewords again only if every Rec-1 feature landed (section 6.4) | A, then C1/D/E1/E2 |
| E3 | EXCLUSIONS.md fails its oracle: 131 sites, 116 uncited, 112 stale; lists `typeid` and `operator new`; README.1ST says 1.4 | true: `tools/exclusions --check` reproduced word for word today; EXCLUSIONS.md:133 and :513 still list `typeid`; README.1ST:197 "version 1.4, sealed 18-09-2026"; CLAUDE.md:29 "101 refusal sites" | FIX: regenerate, re-cite, gate (see D4, Rec 3) | A |
| E4 | cpp11 is the compiler RIDE invokes; Compiler-Cppi byte-identical submodule; only one of three back-end paths runs by default; Debug bypasses MASM; `linkTi()` links rts6740 | true (submodule at 2dd633b; `toolchain.cpp:773-781` F5 runs the `.s` on vm6747; `:397,941-948` Debug forces `-masm=gnu`; `Driver.cpp:734` `rts6740_elf.lib`) | FIX (D2, D3 docs, Rec 5, Rec 6) + DOCUMENT | G, B, F |
| E5 | MASM path broken and untested: 10/379 refused, suite never runs the dialect | true by reading: `Masm.cpp:471-474` opens `.data SEGMENT ... COMDAT(...)` (`.data` is a MASM directive); the `$LNleave$` duplicate needs the box to reproduce; `run-cases.cmd:20-22` says the dialect ran once by hand | FIX (Rec 2) | F |
| E6 | Sibling tools verified against TI/Microsoft artefacts; gates self-referential; TI runs are manual campaigns | partly: `tests/tms6747.sh` runs the sim6747 leg against RTS6x since 07-10; `verify-three` has a `c6747` leg (`c6747-three` + `release-check`) since 05-10 - but RTS6x `referee`, LNK6x/LINK corpus and `o2run` are still by hand | FIX (Rec 7) + DOCUMENT | H |
| E7 | Parallelism real per file, shallow elsewhere; RIDE serialises C6000 compiles with `&&` | true (`toolchain.cpp:709-716`) | FIX (P3) + DOCUMENT in RIDE README and cpp11 README | G, B |
| E8 | No fabricated outputs, no silent fallback | true | DOCUMENT - one sentence in README.md "What is verified" that the review looked and found none, dated | A |

### 1.2 Section 2 - feature matrix (every row marked Missing, Partial, broken or Claimed-but-broken)

Verification is by the refusal site `tools/exclusions` lists today (line numbers
are today's, the review's were a few lines off) unless said otherwise.

| ID | Feature | Today | Disp. | WS | Test |
| --- | --- | --- | --- | --- | --- |
| F01 | raw strings `R"(...)"` | true: `Lexer.cpp:393` refuses by name | IMPLEMENT - lexer reads `R"delim(...)delim"` with every prefix (`u8R`, `LR`, `uR`, `UR`); the preprocessor's `#if` text rewriter and the digraph/alternative-token pass must skip a raw literal whole | D | `raw-string-literal.cpp` (delimiters, newlines, `)"` inside, `#if` with one, `%:` inside one) |
| F02 | user-defined literals | true: `ParserType.cpp:1957` | REFUSE-BY-NAME (keep; re-cite). Design note: needs `operator""` lookup and a literal operator template; not in 48 h | A | `.error` case exists? If not, `udl-refused.cpp` |
| F03 | `auto` from braced list | true: `ParserTemplate.cpp:1085` | IMPLEMENT after F24/F25: `auto x = {1,2};` deduces `std::initializer_list<int>` (header exists); `auto x{1}` is C++11-ambiguous and stays refused naming N3922 | E2 | `auto-braced-list.cpp` |
| F04 | `constexpr` constructors / literal types | true: `ParserType.cpp:587` | IMPLEMENT (runtime half): accept `constexpr` on a constructor as an ordinary constructor, since a constexpr function may always run at run time; a `constexpr` *object* of that class, or a use of it where a constant is required, is refused by name ("a constexpr object of class type is not a constant here - the evaluator has no object model"). CONFORMANCE.md records the half | C1 | `constexpr-constructor.cpp` + `constexpr-class-object-refused.cpp` |
| F05 | `alignas` on a local | true: `Parser.cpp:831` | REFUSE-BY-NAME (keep; frame realignment on four targets is not 48-h work); design note | A | existing refusal case re-cited |
| F06 | `enum class` | true: `ParserType.cpp:1394`; `enum E : short` works | IMPLEMENT: a scoped enumeration is a distinct type that keeps its underlying type's size, converts to an integer only by a cast, and whose enumerators are reached by `E::a` only; overload ranking treats it as a distinct type; mangling unchanged (an enumeration already keeps its name on both ABIs); `switch` over one; `enum class E;` opaque declaration | C1 | `enum-class.cpp`, `enum-class-no-implicit-int-refused.cpp`, `enum-class-unscoped-name-refused.cpp`; names.sh |
| F07a | alias declaration `using X = T;` | true: `ParserConst.cpp:16` (three doors: file, block, class) | IMPLEMENT: same table a typedef enters, all three doors | C2 | `alias-declaration.cpp` |
| F07b | alias templates | true: `ParserTemplate.cpp:376` | IMPLEMENT: `template <class T> using X = Y<T>;` recorded as a template whose instantiation substitutes and names a type; usable wherever a type is | E1 | `alias-template.cpp`; names.sh |
| F08 | inline namespaces | true: `ParserType.cpp:1614` | IMPLEMENT: `inline namespace N {}` makes N's names findable in the enclosing namespace (`qualifyForLookup` tries it as if a using-directive stood there); mangling spells `N` as clang does (measure: `_ZN1A1N1fEv`) | C2 | `inline-namespace.cpp`; names.sh |
| F09 | namespace alias | true: `ParserTopLevel.cpp:81` | IMPLEMENT: `namespace A = N;` and `namespace A = N::M;` enter an alias the namespace lookup follows | D | `namespace-alias.cpp` |
| F10 | unknown attributes are hard errors | true: `ParserType.cpp:1580`, `:1828` | IMPLEMENT: [dcl.attr.grammar]/5 - an attribute-token the implementation does not recognise is ignored; `[[noreturn]]` and `[[carries_dependency]]` keep their reading; the attribute is skipped by balanced brackets wherever the grammar allows one; `alignas` untouched; `-pedantic-errors` clang accepts an unknown attribute (warning only) | C2 | `attribute-ignored.cpp` |
| F11 | `throw(T)` dynamic specification | true: `ParserConst.cpp:87` | REFUSE-BY-NAME (keep; deprecated in C++11, run-time list check; `throw()` is read) | A | existing case re-cited |
| F12 | `override` / `final` | true: `ParserType.cpp:1006` refuses by name; class-head `final` refused too | IMPLEMENT: `override` is a check - the slot search must find a base's virtual with that signature or the program is refused; `final` on a member refuses a later override, on a class head refuses derivation; both contextual (`int final = 3;` stays legal) | C1 | `override-final.cpp`, `override-no-base-refused.cpp`, `final-member-refused.cpp`, `final-class-refused.cpp` |
| F13 | `= default` / `= delete` | true: `ParserConst.cpp:26` | IMPLEMENT: `= default` declares the special member as the implicit one would be (trivial where it is, synthesised where it has work - the existing `declareImplicitSpecials` road) on the first declaration; `= delete` records a deleted function any use of which is refused by name, overload resolution still ranking it; both at the two doors (constructor, member function) by one helper | C1 | `defaulted-members.cpp`, `deleted-function-refused.cpp`, `deleted-copy-refused.cpp`; names.sh |
| F14 | delegating constructors | true: `ParserTopLevel.cpp:1097` | IMPLEMENT: a mem-initialiser naming the class itself must be the only entry, calls the target constructor on `this` (C1 on Itanium, cl's flag passed through on Microsoft), and the object counts as constructed when it returns ([class.base.init]/6) | D | `delegating-constructor.cpp`, `delegating-not-alone-refused.cpp`; names.sh |
| F15 | inheriting constructors / using-declarations in a class | true: `ParserType.cpp:563` | REFUSE-BY-NAME (keep both; re-cite). Design note in the handover: a class using-declaration brings a base's overload set into the derived set with the derived access; inheriting constructors synthesise one forwarding constructor per base constructor - both want the forwarding-reference machinery of F23 first, and neither fits beside C1's queue in 48 h | C2 (note) | existing refusal cases re-cited |
| F16 | NSDMI brace form `int b{2};` | true: `ParserType.cpp:1144`, `:1134` | IMPLEMENT: `int x{5};` and `int x = {5};` on a member read as the member initialiser the `=` form already is, narrowing checked as [dcl.init.list]/7 asks | C2 | `member-brace-initialiser.cpp`, `member-brace-narrowing-refused.cpp` |
| F17 | `thread_local` | true: `Parser.cpp` `pending[]` | REFUSE-BY-NAME (keep). DECLINE implementing: thread-local storage differs per target (TLS on ELF/Mach-O/COFF, none on the C6000); design note names the four ABIs' models | A | `.error` case exists via pending table; re-cite |
| F18 | trailing return type | true: `ParserTopLevel.cpp:623` | IMPLEMENT: `auto f(params) -> T` reads T after the parameter list, in the scope of the parameters (so `decltype(a + b)` works); a lambda's `-> T` already does | D | `trailing-return-type.cpp`; names.sh |
| F19 | `friend class X;` | true: `ParserType.cpp:692` | IMPLEMENT: records the class in the friend table `isFriendOf` consults, so every member function of X passes the five access checks; `friend struct X;` and a forward-declared X too | C2 | `friend-class.cpp`, `friend-class-not-friend-refused.cpp` |
| F20 | const member in a mem-init list | true: `ParserTopLevel.cpp:1050` | IMPLEMENT: the store into a const member from its own constructor's initialiser list is the initialisation, not an assignment - build it against the unqualified member type | D | `const-member-init.cpp` |
| F21 | anonymous unions | true: `ParserType.cpp:783` | IMPLEMENT: the union's members become members of the enclosing class at the union's offset, sharing storage; a union with a non-trivial member stays refused by name as before | C2 | `anonymous-union.cpp` |
| F22 | lambda → function pointer | true: `ParserOverload.cpp:1146` | IMPLEMENT: a captureless closure gets a conversion function to `R (*)(Args...)` returning the address of a synthesised static function that calls `operator()` on a null closure (nothing in it is read) - what both oracles emit; measured names (`_ZZ4mainENK3$_0cvPFiiEEv` shape) | E2 | `lambda-to-function-pointer.cpp`, `lambda-capturing-no-pointer-refused.cpp`; `.nonames` for the closure numbering as `lambda.nonames` already records |
| F23 | forwarding references / reference collapsing / `std::forward` | true (claimed-but-broken): `deduceOne` has no rvalue-reference rule; `ParserOverload.cpp:615` makes `T &&` not viable for an lvalue | IMPLEMENT (Rec 8): [temp.deduct.call]/3 - a parameter `T &&` with T a template parameter and an lvalue argument of type U deduces T as `U &`, and [dcl.ref]/6 collapses `U & &&` to `U &`; collapsing at every substitution of a reference to a reference (which today is refused as "a reference to a reference"); `std::forward` and `std::move` in `<utility>` written once this works; `emplace_back` follows in WS-L | E1 | `forwarding-reference.cpp` (lvalue, const lvalue, rvalue, forward to by-value/by-ref/by-rvalue-ref overloads, nested), `reference-collapsing.cpp`; names.sh (`_Z1fIRiEvOT_` shape measured) |
| F24 | list-initialisation forms `P p{1,2}`, `T{...}`, `new T{...}`, braced call arguments | true: `ParserInit.cpp:932`, `ParserExprNew.cpp:1133`, braced arguments unread | IMPLEMENT: direct-list-init `P p{a,b}` and `P p = {a,b}` for a class with constructors pick a constructor by [over.match.list] (initializer_list constructors first, then the rest), narrowing checked; `T{...}` as an expression is the temporary `classTemporary` already builds; `new T{...}`; a braced argument `f({1,2})` copy-list-initialises the parameter | E2 | `list-init-constructor.cpp`, `list-init-expression.cpp`, `new-list-init.cpp`, `braced-argument.cpp`, `list-init-narrowing-refused.cpp` |
| F25 | `initializer_list` as an argument `sum({1,2,3})` | true (the declaration form works) | IMPLEMENT with F24: a braced argument to an `initializer_list<T>` parameter builds the backing array in the caller's frame, as the declaration form does | E2 | `initializer-list-argument.cpp` |
| F26 | `std::nullptr_t` undeclared | true: `include/cstddef` has no `nullptr_t` | IMPLEMENT: `typedef decltype(nullptr) nullptr_t;` in `<cstddef>` | L | `nullptr-t.cpp` |
| F27a | range-for over a class whose iterator is a class | true: `ParserStmt.cpp:570` | IMPLEMENT: the loop calls `operator!=`, `operator++` and `operator*` through the ordinary operator resolution, the array/pointer path untouched (golden says so) | D | `range-for-class-iterator.cpp` |
| F27b | range-for with free `begin(r)`/`end(r)` | true: `ParserStmt.cpp:535` | IMPLEMENT: [stmt.ranged]/1's fallback through the ADL `lookupKeys` operators already use | D | `range-for-free-begin.cpp` |
| F27c | range-for over a temporary | true: `ParserStmt.cpp:528` | IMPLEMENT: the range temporary is materialised into a frame slot that lives to the loop's end and is destroyed after it (the closure-slot shape), `auto &&__range` bound to it | D | `range-for-temporary.cpp` (ledger) |
| F27d | range-for over a braced list | true: `ParserStmt.cpp:614` | IMPLEMENT after F25: lower to an `initializer_list` backed by a frame array | E2 (after D's F27a merge) | `range-for-braced-list.cpp` |
| F28 | non-type parameter packs | true: `ParserTemplate.cpp:63` | REFUSE-BY-NAME (keep; a second binding list and expansion); design note | A | existing refusal re-cited |
| F29 | `static_cast<D &>(base_ref)` and `static_cast<D *>(base_ptr)` downcast | true: `ParserExpr.cpp:286`; the pointer downcast is also absent (CLAUDE.md "a base-to-derived downcast is a static_cast and is not here") | IMPLEMENT: [expr.static.cast]/2 and /11 - the inverse of the base conversion `anyBaseOffset` already computes, never through a virtual base (refused by name), null kept null | E2 | `static-cast-downcast.cpp`, `static-cast-virtual-base-refused.cpp` |
| F30 | default template arguments on function templates | partly: `template-default-argument.cpp` covers class templates and explicit lists; a deduced call leaves a trailing defaulted parameter unbound | IMPLEMENT: after deduction, an unbound parameter with a default takes it (tokens replayed with the earlier parameters bound), before the viability check | E1 | `function-template-default-argument.cpp`; names.sh |
| F31 | explicit specialisation of function templates | true: `ParserTemplate.cpp:538` | IMPLEMENT: `template <> T f<int>(int)` / `template <> T f(int)` declares the specialization under the primary's key, mangled from the primary's pattern plus the arguments (`_Z5twiceIiET_S0_` is already how an instantiated one is spelled), its body replacing the replay | E1 | `function-template-explicit-specialization.cpp`; names.sh |
| F32 | template template parameters | true: `ParserTemplate.cpp:43` | REFUSE-BY-NAME (keep); design note | A | existing refusal re-cited |
| F33 | `extern template` | true: `ParserType.cpp:1594` | IMPLEMENT as accept: the declaration is read and suppresses nothing - every specialization here is weak/COMDAT, so a definition elsewhere folds with it; CONFORMANCE.md records that nothing is suppressed | C2 | `extern-template.cpp` (two-file case: declared in one, instantiated in the other, links) |
| F34 | out-of-line constructor of a class template | true: `ParserTemplate.cpp:550` region; refused by name | IMPLEMENT: `template <class T> C<T>::C(...)` recognised by `atUntypedMemberDefinition` with a template-id, recorded on the `TemplateDecl` and replayed like a member | E1 | `template-out-of-line-constructor.cpp`; names.sh (the four cases that define constructors inline for this reason lose their `.nonames` lines where they can) |
| F35 | two-phase lookup | true by design (token replay; CONFORMANCE.md) | DECLINE: the decision is recorded in CLAUDE.md "Decisions already taken" and CONFORMANCE.md; this round adds the over-acceptance test `tests/open/two-phase-lookup-late-name.cpp` (clang refuses, cpp11 accepts) so the divergence is counted, and a design note of what a dependent AST would cost | A | `tests/open` entry |
| F36 | ref-qualifiers | true: `ParserType.cpp:986` | REFUSE-BY-NAME (keep); design note | A | existing refusal re-cited |
| F37 | polymorphic virtual base on x86_64-windows | true: refused by name; cl's vtordisp layout measured in CLAUDE.md "The Microsoft virtual-base layout" | REFUSE-BY-NAME (keep; the measurement exists, the implementation is a round of its own) | A | `.notarget` lines already carry the reason |
| F38 | Windows EH gaps: class-typed throw, rethrow, try-in-catch | true: refused by name per target (`ParserStmt.cpp` Microsoft branches) | REFUSE-BY-NAME (keep) + design note: the ThrowInfo chain for a class (`_CT??_R0?AUE@@@8`, copy constructor and destructor slots) and `_CxxThrowException` rethrow with null arguments; nested funclet naming. Not 48-h work beside D15 | F (note) | existing `.notarget` reasons |
| F39 | tms6747 EH: 26 failures with RIDE's lnk6x on 06-10 | fixed: LNK6x `c8d3d9e` (1.1) composes the unwind index after `.c6xabi.extab` is placed; `tests/tms6747.sh` runs every case through lnk6x + RTS6x on sim6747 since 07-10 | DOCUMENT (the fix and the gate) + VERIFY on TI's simulator in WS-H's scheduled `release-check` run | H | `release-check` record |
| F40 | `goto` out of a handler; function-try-block | true: `ParserStmt.cpp:2110`, `ParserTopLevel.cpp:1235` | REFUSE-BY-NAME (keep; both have reasons written where they refuse) | A | existing refusal cases re-cited |
| F41a | `dynamic_cast` to a reference | true: `ParserExprNew.cpp:660` - but the reason it gives ("no C++ standard library to throw bad_cast from") is stale: a class throw works on both Itanium targets and `<typeinfo>` exists | IMPLEMENT on Itanium: a failed cast throws `std::bad_cast` (declared in `<typeinfo>` over the runtime's own, as `<new>` declares `bad_alloc`); x86_64-windows refuses by name (class throw refused there) | E2 | `dynamic-cast-reference.cpp` with `.notarget` for x86_64-windows and tms6747 if RTS6x lacks `bad_cast` (check RTS6x `src/cxx`) |
| F41b | `dynamic_cast` across a class with more than one base | partly: the refusal at `:720` says the type_info shape is missing, and `__vmi_class_type_info` has been emitted since 10-06 ("A class's own vptr goes in front of its bases - and then the type_info") | IMPLEMENT on Itanium: lift the refusal and measure `__dynamic_cast` against clang on two-base, diamond and cross casts; Microsoft: `__RTDynamicCast` reads the hierarchy descriptor that is already written - measure with cl, and keep the refusal by name where it disagrees | E2 | `dynamic-cast-multiple-bases.cpp`; names.sh |
| F42 | RTTI row: supported, but docs say refused | true: README.md:136-138, EXCLUSIONS.md:133,513 | DOCUMENT (FIX the docs) | A | - |
| F43 | overloaded / placement `operator new` supported, docs say refused | true: README.md:136-137, :193 | DOCUMENT (FIX the docs) | A | - |
| F44 | `__COUNTER__` absent (an extension; the review notes it) | true | IMPLEMENT: four lines in the preprocessor, documented as an extension | D | `preprocessor-counter.cpp` |

### 1.3 Section 2 - the two library rows

| ID | Finding | Today | Disp. | WS | Test |
| --- | --- | --- | --- | --- | --- |
| L01 | `map`/`set` are sorted vectors | true (`include/map:3,109`) and said so in the header | DECLINE for this round + DOCUMENT in README.md's library paragraph: O(n) insertion and pointer invalidation on insert differ from the standard's; a balanced tree is its own round (design note: a red-black tree with node-stable iterators, `~6 h` for map and set together, after WS-L's queue) | L (doc) | - |
| L02 | `sort` is O(n²) | true (`include/algorithm:63-84`, insertion sort) | IMPLEMENT: our own introsort (median-of-three quicksort to a depth bound, heapsort past it, insertion sort under 16) - [alg.sort] asks O(n log n); `stable_sort` stays absent and is named as absent in the header | L | `algorithm-sort-large.cpp` (10^5 elements, ordered/reversed/equal keys, with a comparator) |
| L03 | `vector` leaks non-POD elements on `erase`/`pop_back`/`clear` | true (`include/vector:14-22`, written down) | IMPLEMENT: explicit destructor call `p->~T()` on the dropped slots (landed 09-26), copy/move of the tail through the copy constructor | L | `vector-erase-destroys.cpp` (ledger) |
| L04 | `at()` unchecked | true (`include/vector:103`, README.md:128-131) | IMPLEMENT: throws `std::out_of_range` on Itanium and tms6747 (RTS6x); on x86_64-windows, where a class throw is refused, `at()` calls a `_cxx11_range_fail()` that prints and aborts - the header says so under `#ifdef _WIN32` and CONFORMANCE.md records it | L | `vector-at-throws.cpp` with `.notarget` x86_64-windows, plus `vector-at-aborts-ms.cpp` |
| L05 | `setfill` silently ignored | true (`include/iomanip:26`) | IMPLEMENT: a fill character on the stream, used by the width padding `setw` already applies | L | `iomanip-setfill.cpp` |
| L06 | no `emplace_back` | true | IMPLEMENT after F23 lands: `template <class... A> void emplace_back(A &&...a)` placement-constructing `T(std::forward<A>(a)...)`; if E1 slips, `emplace_back(const A &...)` by value with the limitation named in the header | L (after E1) | `vector-emplace-back.cpp` |
| L07 | no `to_string` / `stoi` | partly: `to_string` exists (`include/string:367-389`); `stoi`/`stol`/`stoul`/`stoll`/`stod` do not | IMPLEMENT the `sto*` family over `strtol`/`strtod`, with `invalid_argument` and `out_of_range` on Itanium/tms6747 and the Windows abort rule of L04 | L | `string-stoi.cpp` |
| L08 | `<memory>` | true | IMPLEMENT `unique_ptr<T>`, `unique_ptr<T[]>`, `shared_ptr<T>` (non-atomic count; `<atomic>` absent is said in the header), `make_shared` by value; `weak_ptr` absent and named | L | `memory-unique-ptr.cpp`, `memory-shared-ptr.cpp` (ledger) |
| L09 | `<functional>` | true | REFUSE-BY-NAME + design note: `std::function` wants a partial specialization on a function type pattern `R(Args...)` and type erasure; `std::less` and friends fit in 1 h and go in | L | `functional-less.cpp` |
| L10 | `<tuple>` | true | REFUSE-BY-NAME + design note (the recursive `Tuple` in `tests/cases/tuple.cpp` is the shape; `get<I>` and `make_tuple` need F23 for `tie`) | L | - |
| L11 | `<array>` | true | IMPLEMENT: `array<T, N>` as an aggregate with `begin`/`end`/`size`/`operator[]`/`at`/`fill` | L | `array-header.cpp` |
| L12 | `<unordered_map>`, `<unordered_set>` | true | REFUSE-BY-NAME + design note (separate chaining over `vector`, `std::hash` for the integral types and `string`) | L | - |
| L13 | `<thread>` | true | DECLINE: no threads on the C6000 and no `thread_local`; named absent | L | - |
| L14 | `<atomic>` | true | DECLINE as L13; named absent | L | - |
| L15 | `<chrono>` | true | DECLINE this round (needs `<ratio>`-style arithmetic over templates with non-type packs F28); named absent | L | - |
| L16 | `<regex>` | true | DECLINE; named absent | L | - |
| L17 | `<random>` | true | DECLINE this round; named absent (design: `minstd_rand`, `mt19937`, `uniform_int_distribution` are specified by the standard and are 3 h) | L | - |
| L18 | `<cstdarg>` (`<stdarg.h>` works) | true | IMPLEMENT: the C wrapper, like the five that exist | L | `cstdarg.cpp` |
| L19 | "cannot find" for a standard header is not a named refusal | true | REFUSE-BY-NAME: the preprocessor answers `#include <tuple>` with "the standard header <tuple> is not provided by this compiler yet" for every C++11 header name this tree lacks, and EXCLUSIONS.md cites the table | D (Preprocessor.cpp) | `missing-standard-header-refused.cpp` |

### 1.4 Section 4 - dodge-condition findings D1-D17

| ID | Finding | Today | Disp. | WS |
| --- | --- | --- | --- | --- |
| D1 | IDE's default C6000 Run executes the `.s` on vm6747, whose Runtime.cpp carries libc and the EH runtime natively | true (`toolchain.cpp:773-781`; `Runtime.cpp:566,669,1210`) | FIX (Rec 5: Run = sim6747 on the linked `.out`; vm6747 stays as "Emulate (quick)") + DOCUMENT the consequence in help/06 and the architecture page | G, B |
| D2 | `makeTiProgram` returns `ok == true` with no `.out` when asm6x is absent or no runtime/linker | true (`compile.cpp:614 if (as.empty()) return;` and `:665-668`) | FIX: a tms6747 build without a `.out` is a failed build (`result.ok = false`, the reason printed); a project that wants emulate-only says so in its settings | G |
| D3 | Debug builds bypass MASM/LINK for clang + link.exe /DEBUG; the manual says the opposite | true (`toolchain.cpp:397-400, 941-948, 705`; help/07:53, cpp.md:70-73,82, 08-debugging.md:43) | DOCUMENT (FIX the three pages and the architecture page; the behaviour is M10's and right) | B |
| D4 | EXCLUSIONS.md fails its oracle; counts disagree 101/130/131 | true | FIX: regenerate from `tools/exclusions`, re-cite every site, hand-written entries kept with live case links; `--check` added to `make test` and to `verify-three`'s Mac step; CLAUDE.md:29 reads the tool's number | A (document), H (verify-three) |
| D5 | Prompted native fallback: nothing marks the resulting binary | true (`compile.cpp:429-434`) | FIX: the console line names the tools used, and the build log / Output window ends `[built with Visual Studio's ml64 and link.exe, not RIDE's]`; help/07 documents the prompt | G, B |
| D6 | Day-to-day gates self-referential; TI runs manual | partly (see E6) | FIX (Rec 7) + DOCUMENT the beds' sizes and known-differ counts in one verification page | H |
| D7 | `emit.sh` golden is self-generated and never fails | true by design (`emit.sh:12-19`) | DOCUMENT: README.md's suite paragraph says what the golden is for (a before/after of one change, read by hand) and that it is not an oracle; the four-suite count in README stays four with that sentence beside it | A |
| D8 | README.1ST drift (1.4, `-masm` options, `-g` targets, known shortcomings); `-version` prints "PST" | true (`README.1ST:36,37,197,214`; `Driver.cpp:946 "Built %s PST"`) | FIX: README.1ST §6 and README.md generated from `Version.h` by `tools/version-lines` (run by `tools/seal write`, checked by `tools/seal check`); `-masm` list from `--help`; `-g` says all four targets (x86_64-windows CodeView since M10); "Known shortcomings in 1.6" rewritten from EXCLUSIONS; `-version` prints the build's own zone abbreviation (`%Z`) or none | A (docs), F (Driver.cpp) |
| D9 | cpp11 standalone links TI's rts6740; RIDE links RTS6x; RTS6x unreachable from the command line | true (`Driver.cpp:734`; `compile.cpp:659-708`) | FIX (Rec 6): `-rts=<dir>`/auto-detect `lib/rts6x-tms6747` beside `cpp11.exe`; the link line printed names the runtime; README.md and README.1ST describe both and which is chosen when | F |
| D10 | Measurement exemptions: 16 `lambda*` cases exempt; 76 cases with no TI build; `corpus.sh` exits 0 | true (`O2-REPORT-2026-10-04.md`; `corpus.sh:7`) | FIX: WS-H's scheduled TI run includes the 16 lambda cases (no exemption); the 76 are listed by name with the reason each cannot build for tms6747 (`.notarget` and `tms6747-lp64.txt`), and the stand-in is said in the report; `corpus.sh` keeps exiting 0 (it is a control, documented so) but prints the split against a recorded baseline and names any movement | H |
| D11 | 27 empty `.nocl` files | fixed: `find tests/cases -name '*.nocl' -size 0` finds 0 of 110 today | DOCUMENT in the handover (verified count) | A |
| D12 | Silent semantic downgrades: volatile → -O0 with a note; `setfill`; `at()`; unknown attributes | partly: volatile is announced on stderr (`Driver.cpp:1133`) - a note, not silence; the other three are F10, L04, L05 | FIX: volatile's note stays but is also printed in the success line's summary; `setfill` L05, `at()` L04, attributes F10 implemented | F, L, C2 |
| D13 | Seal ≠ verification: CRC32 over sources; `bin/cpp11.exe` sealed 03-10 vs MASTER 08-10; MASTER names rts6x-1.1 | partly fixed: MASTER.SEAL `644d6d7` names `rts6x-1.2.dat` and `sim6747-1.2.dat`; `bin/cpp11.exe` is a dev artefact outside the seal; CRC32-over-sources is the user's rule ("Seals: source only") | FIX half (Rec 10): `MASTER.SEAL` gains a second table, "shipped artefacts", with SHA-256 of each `dist/` executable and library written by `release.sh`/`release.cmd` at release time - the source seal stays what it is; DOCUMENT the rule that a dev `bin/` is not sealed | G |
| D14 | RIDE's suite skips compiler-driven cases when tools are absent | true | FIX: `tests/test --require-tools` fails a skip; `verify-three`'s RIDE leg and `release.sh` pass it | G |
| D15 | `-masm=masm` refused by the project's MASM for 10/379; the suite never runs the dialect | true (section 1 E5) | FIX (Rec 2) | F |
| D16 | `release.sh` builds Compiler-Cppi at the default-branch head, not the pin | true (`release.sh:8,44-54`) | FIX (Rec 10): refuse when head ≠ pin unless `ALLOW_UNPINNED=1`, and print both; the same in `release.cmd` | G |
| D17 | ASM6x `c13-packets` differs from TI's compressed object, undocumented | true: no known-list and no README entry names it | FIX: measure the difference (`check.sh` output on the box), record it in `ASM6x/tests/compact/known-differ.txt` with the bytes and the reason, or fix the encoder if the reason is ours | H |

### 1.5 Section 3 - risks named in the toolchain-integration text

| ID | Risk | Today | Disp. | WS |
| --- | --- | --- | --- | --- |
| S1 | 3.0: release can ship an unpinned Compiler-Cppi | = D16 | FIX | G |
| S2 | 3.4: `bin/lib/rts6x-tms6747` lacks `rts6xd.lib`, `shmrt6x.lib`, `shmrt6xd.lib`; a Debug C6000 link from that `bin/` fails; only `make confirm` guards | true (`ls bin/lib/rts6x-tms6747` → `printf6x.lib rts6x.lib`) | FIX: `workspace.mk` builds all six into `bin/lib` as part of the default target; `release.sh`/`release.cmd` run `make confirm` and refuse on MISSING; help/06:125-127 stays true | G |
| S3 | 3.4: RTS6x `make check` is vm6747 + sim6747 only; `referee` manual, no recorded result | true (`RTS6x/tests/run.sh:1-14`, `tools/referee`) | FIX: WS-H runs `referee` once (scheduled) and records `RTS6x/docs/REFEREE-2026-10-09.md`; `referee` gains `--record` writing that file | H |
| S4 | 3.4 / D9: two link lines for one compiler | = D9 | FIX | F |
| S5 | 3.3: LNK6x's pinned known-differ covers the region where the 06-10 defect lived | true (`LNK6x/tests/known-differ.txt`: 3001-31285 bytes per runtime probe, in the unwind index, attributes blob, symbol table) | FIX partly (Rec 7b): shrink by making the unwind-index entries for code without its own (`EXIDX_CANTUNWIND`) match lnk6x's, which is the region that bit; re-pin the attributes blob and symbol table with their byte counts split out per region so an unwind-index change can never hide in them again. Design note for the rest | H |
| S6 | 3.3: SIM6747 FIX-REQUEST five fatal defects found 05-10 | fixed per memory (D1-D8 fixed, 81/81 vs CCS 5.5, SIM6747 1.2) | DOCUMENT: verify `SIM6747/FIX-REQUEST.md` marks each closed with its commit; WS-H re-reads | H |

### 1.6 Section 5 - parallelism weak spots

| ID | Finding | Today | Disp. | WS |
| --- | --- | --- | --- | --- |
| P1 | `Source::fail` calls `std::exit(1)` from a worker thread; other units' diagnostics lost; UB with live threads | true (`Source.cpp:131`) | FIX: `fail` throws `CompileFailed` after writing its text under a mutex; `runJobs` catches per job, records the failure, lets the other jobs finish their diagnostics, and the main thread exits 1 after `join()`. Every `fail` site already assumes no return (it is `[[noreturn]]`-shaped), so the throw changes no caller; the three `Trial` catches catch `SubstitutionFailure` only and are unaffected. Gate: a two-file compile with an error in each prints both | F |
| P2 | Racy `static int t` in `C6xPipe.cpp:21` (`tracing()`) | true, benign (the same value computed by every thread) | FIX: read once in `c6xSchedule`'s caller on the main thread, or `static const int t = read()` (C++11 guarantees the initialisation is thread-safe) - one line | F |
| P3 | RIDE compiles C6000 sources serially with `&&` | true (`toolchain.cpp:709-716`) | FIX: one `cpp11 -S -arch tms6747 <all sources> -o <dir>` invocation per build, cpp11's own pool doing the parallel work (Driver already takes several inputs and `-j`); the per-file `.s` names unchanged; RIDE README's build page says so | G |
| P4 | Not TSAN-verified | true | FIX: `make tsan` target (clang `-fsanitize=thread` on the Linux box is unavailable - no clang; g++ 11 has `-fsanitize=thread`) runs `tests/run.sh` with `-j4`; result recorded in the handover | F |

### 1.7 Section 6 - stage-verification gaps, row by row

| ID | Row | Gap | Today | Disp. | WS |
| --- | --- | --- | --- | --- | --- |
| V1 | Preprocessor/lexer | no `-E` diff against clang | true | FIX: `tests/preprocess.sh` runs every `preprocessor-*` case through `cpp11 -E` and `clang -E -P`, whitespace-normalised, and diffs; runs on the box under Git bash with VS clang | H |
| V2 | Parser/sema | 224 `.error` "passes" are the compiler saying no | true (628 cases: 402 `.expected`, 224 `.error` + the review's own count) | DOCUMENT: README.md's suite paragraph splits the two numbers and says what each proves; `run.sh` prints them apart | A |
| V3 | Codegen | `.nonames` on 202 cases (32%) | true (202 `.nonames` of 628) | FIX partly: WS-E1's F34 removes the inline-constructor reason from the cases that carry only it; `tests/names.sh --reasons` prints the `.nonames` lines grouped by reason so the count is a table rather than a number; the table goes in the verification page | H |
| V4 | Optimizer | no IR verifier; 8 asserts | true | DECLINE an IR verifier this round + DOCUMENT: `tools/identical.sh` and `run.sh -O2` on the Linux box and the Windows cases at -O2 (since 07-10) are the gates; design note for a `Mir` consistency check (def-before-use, label targets exist) as a later round | A (doc) |
| V5 | x86_64-windows MASM spelling | nothing in the suite | = D15 | FIX | F |
| V6 | MASM assembler | no cpp11 EH/COMDAT objects in its bed | true | FIX (Rec 2): `MASM/tests/cpp11/` gains the `.asm` of `try-catch`, `throw-class`, `template-static-member-on-use`, `handler-exit-ms` as cpp11 emits them, assembled and compared with ml64's objects by `coffcode.py` | F |
| V7 | LINK | tiny bed; `corpus.sh` box-only; 09-23 Shalimar images segfaulted | true | FIX (Rec 2): `LINK/tests/probes` gains cpp11-made objects with EH tables and COMDAT data (the four above plus a two-file COMDAT program), linked by LINK and by link.exe, run, and compared by `pediff.py`; the 09-23 segfault is re-run and either closed with its commit or opened as a probe | F |
| V8 | C6000 codegen | TI simulator only in manual campaigns | partly (`verify-three c6747` exists) | FIX (Rec 7) | H |
| V9 | C6000 optimizer | `CPP11_C6XLIVECHECK` opt-in | true | FIX: `tests/tms6747.sh` sets `CPP11_C6XLIVECHECK=1` at -O1/-O2/-Os; the cost was measured under 1% on 07-10 | F |
| V10 | ASM6x | one compact probe differs, undocumented | = D17 | FIX | H |
| V11 | LNK6x | pinned differences cover the unwind-index region | = S5 | FIX partly | H |
| V12 | SIM6747 | memory system not modelled (cycle.Total) | true by design | DOCUMENT in `SIM6747/README` (already says cycle counts are CPU-only? WS-H verifies and adds the sentence if absent) | H |
| V13 | VM6747 | native libc/EH; cannot see encoding, linking, runtime | = D1 | FIX (Rec 5) + DOCUMENT in `VM6747/Emulator/README` | G, B |
| V14 | RTS6x | no recorded referee result | = S3 | FIX | H |
| V15 | IDE integration | skips when tools absent | = D14 | FIX | G |
| V16 | "Still not verified": whether every `.expected` came from clang | open | FIX: `tools/windows/expected-from-clang.cmd` recompiles every `.expected` case with VS clang on the box and diffs its output against the file; the differences (evaluation-order and elision-dependent cases excepted by name) are listed in the verification page and each one is fixed or explained | H |

### 1.8 Section 7.1 - entity diagram vs code (`RIDE-4.7/docs/ride-5.0-architecture.html`)

| ID | Element | Today | Disp. | WS |
| --- | --- | --- | --- | --- |
| A1 | cpp11 backend list omits `CodeView.cpp`/`CodeViewTypes.cpp` and the target files | true | DOCUMENT: list them | B |
| A2 | x86_64-windows assembler/linker row wrong twice (Debug = clang + link.exe /DEBUG; Release with nothing named = the project's masm/link beside cpp11) | true | DOCUMENT: redraw the row as two lines, Debug and Release | B |
| A3 | tms6747 asm6x → lnk6x → rts6x → `.out` is the F4/Run-on-Simulator path, not F5 | true until Rec 5 lands | DOCUMENT after WS-G decides (section 5): the diagram draws Run = sim6747 on the `.out`, Emulate = vm6747 on the `.s` | B (after G's hour-3 note) |
| A4 | "vm6747 runs the .s" does not say it carries its own libc/EH | true | DOCUMENT: one sentence on the vm6747 box | B |
| A5 | "The TI C6747 board itself" has no code behind it | true | DOCUMENT: keep the box, label it "via CCS, not RIDE" | B |
| A6 | installed `bin/lib/rts6x-tms6747/` lists six libraries; the dev `bin/` holds two | true | FIX (S2) - the page is then right | G |
| A7 | seals table stale (ride-5.0.dat 35BD9D0E, cxx1-1.5, lnk6x-1.0, sim6747-1.1, rts6x-1.0, master 1732D0A4) on a page dated 08-10 | true (page lines 281-297) | DOCUMENT: the table is generated from `MASTER.SEAL` by `tools/master-seal --html`, so it cannot drift again; page title and banner read "RIDE 5.1"; the folder name `RIDE-4.7` is explained in one line (the checkout's name is historical) | B (page), G (tool) |
| A8 | edge not drawn: cpp11 → RTS6x (and shmrt-tms6747) are compiled by cpp11 | true | DOCUMENT: draw the edge | B |
| A9 | edge not drawn: cpp11 standalone → TI lnk6x + rts6740 | true until D9 lands | DOCUMENT: draw both link lines and which is default | B |
| A10 | edge not drawn: prompted native retry | true | DOCUMENT | B |
| A11 | edge not drawn: Debug on x86_64-windows → clang → link.exe /DEBUG → cdb (M10) | true | DOCUMENT | B |
| A12 | parallelism undrawn: cpp11's pool, `-j`, RIDE's serial C6000 compiles | true | DOCUMENT in the page's "how a build runs" paragraph, after P3 | B |
| A13 | stage verification undrawn | true | DOCUMENT: a "what verifies each tier" table on the page, generated from the verification page WS-H writes | B |
| A14 | "ml64" drawn as the IDE's x86_64-windows assembler; RIDE never names it | true (help/07:53) | DOCUMENT (with T2) | B |

### 1.9 Section 7.2 - disclosure rows

| ID | Finding | Disclosed today? | Disp. | WS |
| --- | --- | --- | --- | --- |
| M1 | D1 partly disclosed; help/06:98 lists asm6x in the F5 run | true | DOCUMENT (with A3/A4) | B |
| M2 | D2 undisclosed; help/06:128-131 says builds end `[linked ...]` | true | FIX D2 + DOCUMENT the failure wording | G, B |
| M3 | D3 contradicted by three pages | true | DOCUMENT (T2, T3) | B |
| M4 | D15 undisclosed; diagram says "cpp11 gets -masm=masm" | true | FIX D15 + DOCUMENT | F, B |
| M5 | D9 disclosed in cpp11's READMEs, not RIDE's | true | DOCUMENT in help/06 after D9 | B |
| M6 | D5 only in RIDE README | true | DOCUMENT in help/07 | B |
| M7 | parallelism model undisclosed in RIDE docs | true | DOCUMENT | B |
| M8 | stage verification undisclosed in RIDE docs | true | DOCUMENT (A13) | B |
| M9 | D16 disclosed in a `release.sh` comment only | true | FIX D16 + DOCUMENT in `packaging/README` | G |

### 1.10 Section 7.3 - documentation contradicting the code or itself

| ID | Contradiction | Today | Disp. | WS |
| --- | --- | --- | --- | --- |
| T1 | "three targets": help/07:49,53-56; help/README.md:33-34; help/c.md:22; help/01:92 - the code and help/08:66 have four | true (each line read) | DOCUMENT: four, with tms6747 in every table | B |
| T2 | help/07:53 "x86_64-windows: MASM, assembled by ml64" | true | DOCUMENT: Debug = clang (CodeView), Release = RIDE's masm and link, or Visual Studio's when named | B |
| T3 | help/cpp.md:70-73,82 "DWARF on two ... on x86_64-windows MASM and no line table"; help/08:43 "no - MASM carries no line table" | true | DOCUMENT: CodeView on x86_64-windows since M10 (W1 and W5 in CLAUDE.md), cdb steps and reads locals | B |
| T4 | help/01:110-111 "no optimiser of its own" vs help/07:78-84 describing -O1/-O2; help/01 never names tms6747, vm6747, sim6747, asm6x, lnk6x, RTS6x | true | DOCUMENT: help/01 says RIDE has none and the compilers do; one paragraph on the C6747 half of the product | B |
| T5 | architecture page: seals table, title "RIDE 5.0" vs about.cpp "5.1" vs folder "RIDE-4.7"; "ride-5.0.dat" never existed | true | DOCUMENT (A7) | B |
| T6 | help/06:125-127 promises rts6xd.lib, printf6xd.lib in the install | true (the page), and the libraries are missing from the dev `bin/` (S2) | FIX S2 so the page is true | G |
| T7 | README.1ST §6 "1.4, 18-09-2026", §1 `-masm` list; README.md "Known shortcomings in 1.4" (typeid, operator new, placement new); CLAUDE.md 101 vs EXCLUSIONS 130 vs tool 131 | true | FIX (D8, D4) | A |
| T8 | README.1ST §3 "ISO C++11, and a large part of it" | true | FIX (E2 headline) | A |

### 1.11 Section 8 - recommendations

| ID | Recommendation | Disp. | WS |
| --- | --- | --- | --- |
| R1 | Fix the headline until enum class, override/final, =default/=delete, delegating/inheriting ctors, alias declarations, forwarding references, list-init land | FIX headline now (A); IMPLEMENT every item but inheriting constructors (F15, REFUSE-BY-NAME with design) | A, C1, C2, D, E1, E2 |
| R2 | MASM dialect under the suite; fix `$LNleave` duplicate and `.data SEGMENT COMDAT`; cpp11 EH/COMDAT objects in LINK's bed | FIX | F |
| R3 | Regenerate EXCLUSIONS, gate `--check` in verify-three; generate README version lines from Version.h | FIX | A (docs, Makefile gate), H (verify-three line) |
| R4 | `makeTiProgram` failure states fail the build | FIX (D2) | G |
| R5 | IDE tms6747 Run executes the linked `.out` on sim6747 by default; vm6747 as explicit quick emulate | FIX | G |
| R6 | cpp11 RTS6x option / auto-detect; stamp the runtime name in the build log | FIX (D9) | F |
| R7 | Schedule TI-simulator legs in verify-three, record results in the tree; shrink LNK6x's pinned counts | FIX (first half whole; second half partly, S5) | H |
| R8 | Implement forwarding references | IMPLEMENT (F23) | E1 |
| R9 | Move the C6000 optimizer onto `Mir`, or run `CPP11_C6XLIVECHECK` in the suite; document the ASM6x compact probe | DECLINE the move this round (design note: what `Mir` would need to carry for a VLIW - units, delay slots, packets - and why the text pass was chosen); FIX the two small halves (V9, D17) | F (livecheck + note), H (c13) |
| R10 | Bind seals to artefacts and pins: SHA-256 of shipped files in MASTER.SEAL; `release.sh` refusing on pin mismatch or missing libs | FIX (D13 artefact table, D16, S2) | G |

**Register totals: 160 items** (E 8, F 49 counting the a/b/c/d splits, L 19, D 17,
S 6, P 4, V 16, A 14, M 9, T 8, R 10). IMPLEMENT 44, FIX 57, DOCUMENT 35,
REFUSE-BY-NAME 15, DECLINE 9 (an item with two dispositions is counted under the
first named).

---

## 2. What the review gets wrong, or that the trees have moved past

- **D11**: no empty `.nocl` exists today (0 of 110). Either the count was from an
  older tree or the files were read as empty on the sandbox; verified by `find -size 0`.
- **L07**: `std::to_string` exists in `include/string` (landed 09-07 for the
  catcher programs); only the `sto*` family is missing.
- **F39**: the 26 tms6747 EH failures were LNK6x's unwind-index placement, fixed
  the same day (LNK6x 1.1); `tests/tms6747.sh` has run every case through
  asm6x + lnk6x + RTS6x on sim6747 since 07-10, so "claimed" is no longer the
  state - WS-H's scheduled TI run is what will say so.
- **D13**: MASTER.SEAL already names `rts6x-1.2.dat` and `sim6747-1.2.dat`
  (`644d6d7`, `4288d92`); the architecture page is what is stale. CRC32 over
  sources only is the user's standing rule for seals, not an oversight; what
  the plan adds is a separate artefact table at release time.
- **F41b**: the refusal text at `ParserExprNew.cpp:720` is stale in the same way
  the review's section 2 RTTI rows are - `__vmi_class_type_info` has been
  emitted since 10-06; the remaining work is the cast's runtime walk, not the
  type_info.
- **D12**: the volatile downgrade is printed to stderr at every such compile
  (`Driver.cpp:1133`), so it is announced rather than silent; what the review
  is right about is that nothing in the success line records it.
- **D6/E6**: `verify-three` has had a scheduled C6747 leg (`c6747-three` and
  `release-check` on CCS 5.5's simulator) since 05-10, and `tests/tms6747.sh`
  runs the sim6747 leg against RTS6x; the manual campaigns left are RTS6x's
  `referee`, `o2run` and the LNK6x/LINK corpora.
- **F10**: in C++11 as published, [dcl.attr.grammar]/5 says an unrecognised
  attribute-token is ignored, so the fix is conformance, not an extension.
- **The review's `-masm` default claim** (section 3.2) is right, and the
  diagram's "Microsoft's link.exe" wrong, exactly as it says; nothing to add.

---

## 3. Phases

| Phase | Hours | Content |
| --- | --- | --- |
| 0 | 0-6 | Truth-telling: WS-A (cpp11 docs, EXCLUSIONS regenerated and gated, version lines, headline), WS-B (RIDE manual, architecture page, seals table generator) - both doc-only, both merged first |
| 1 | 0-24 | Rec-1 language features: WS-C1 (enum class, override/final, =default/=delete, constexpr ctor, friend class), WS-D (delegating ctors, trailing return, const member init, raw strings, namespace alias, range-for, `__COUNTER__`, missing-header refusal), WS-E1 (forwarding references, function-template defaults, alias templates, explicit specialisation, out-of-line template ctor), WS-E2 (list-init, braced arguments, initializer_list args, auto from braces, lambda→pointer, static_cast downcast, dynamic_cast reference and multi-base) |
| 2 | 6-30 | Toolchain: WS-F (MASM dialect under the suite, Masm.cpp fixes, LINK/MASM beds, Driver `-rts`, P1/P2/P4, livecheck), WS-G (RIDE: D2, Rec 5, D5, D14, D16, S2, P3, master-seal artefact table) |
| 3 | 24-44 | WS-C2 (after C1: alias declaration, inline namespace, anonymous union, NSDMI brace, attributes, extern template, F15 design note), WS-L (after E1: library), WS-H (verification: TI legs scheduled, `.expected` provenance, `-E` diff, LNK6x pins, ASM6x c13, referee record, verification page) |
| 4 | 36-48 | Main session: merges in order (section 5), final gates, 122-probe re-measure, headline decision, CLAUDE.md sections, version bumps, reseals, MASTER.SEAL, push |

Concurrency never exceeds six: hours 0-6 A, B, C1, D, E1, E2; hours 6-24 C1, D,
E1, E2, F, G; hours 24-36 C2, L, H, F, G, E2 (E1 and D are done by 24, C1 by 24);
hours 36-44 C2, L, H plus merging.

---

## 4. Workstreams

Each table names the repository, the files the workstream owns (no other
workstream edits them), the items, the gate, and the estimate including gate
time on the box. "Case" means `tests/cases/<name>.cpp` with its clang
`.expected` or `.error`.

### WS-A - cpp11 truth-telling (docs and gates)
- **Repo**: C++Optimize. **Branch** `review/a-docs`.
- **Owns**: `README.md`, `README.1ST`, `docs/EXCLUSIONS.md`, `docs/CONFORMANCE.md`
  (the paragraphs named here), `tools/exclusions` (hand-written-entry support),
  new `tools/version-lines`, `tools/seal` (calls version-lines on `write`, checks
  on `check`), `Makefile` (`test` target: `exclusions --check`, `comment-lines`
  counted as a failure), `src/Version.h` banner text only, CLAUDE.md lines 1-60
  (the headline paragraphs; nothing else in CLAUDE.md), `tests/open/two-phase-lookup-late-name.cpp`,
  new `tools/windows/names-overload.cmd`.
- **Items**: E1, E2 (headline), E3, E8, F02, F05, F11, F17, F28, F32, F35, F36,
  F37, F40, F42, F43, D4, D7, D8 (docs half), D11, V2, V4 (doc), T7, T8, R3 (docs
  and Makefile), and every REFUSE-BY-NAME item's re-citation.
- **Gate**: `tools/exclusions --check docs/EXCLUSIONS.md` → 0 uncited, 0 stale;
  `tools/seal check` → agrees after `version-lines` regenerates the two READMEs'
  version lines; `make test` on the box; the Makefile gate fails when a refusal
  is uncited (prove by adding one and removing it).
- **Estimate**: 6 h.

### WS-B - RIDE documentation and the architecture page
- **Repo**: RIDE-4.7. **Branch** `review/b-docs`.
- **Owns**: `help/*.md` and their rendered `.html`, `docs/ride-5.0-architecture.html`
  (renamed `ride-architecture.html`, with a redirect stub), `README.md` of RIDE
  (the toolchain paragraphs), `packaging/README`.
- **Items**: D3 (docs), D1 (disclosure), D5 (manual), A1-A5, A7 (page; the
  generator is G's), A8-A14, M1-M8, T1-T6 text, E7 (RIDE README parallelism).
- **Sequencing**: hours 0-3 everything that does not depend on G; hour 3 reads
  G's decision note (`docs/review-2026-10-08/g-decisions.md`: Run default, D2
  wording, D5 wording, P3 recipe) and writes A3/M1/M2/M6 accordingly.
- **Gate**: every path:line in section 7.3 re-read and quoted in the handover as
  it now reads; `make help` (the renderer) clean; RIDE `tests/test` on the box
  unchanged (docs only).
- **Estimate**: 6 h.

### WS-C1 - class declarations, first half
- **Repo**: C++Optimize. **Branch** `review/c1-class`.
- **Owns**: `src/parser/ParserType.cpp`, `src/parser/ParserConst.cpp`,
  `src/parser/ParserClass.cpp`, `src/parser/ParserVtable.cpp`, `src/Type.h`,
  `src/Type.cpp`, `src/parser/ParserOverload.cpp` (ranking of a scoped
  enumeration; deleted functions), `src/Mangle.cpp` if a measurement demands it,
  `src/parser/Parser.h` fields these need.
- **Items**: F06, F12, F13, F04, F19 (friend class is `ParserType.cpp:692`, so
  it is here rather than C2), R1 (its part).
- **Order inside**: F12 (3 h) → F13 (5 h) → F06 (8 h) → F04 (3 h) → F19 (1.5 h).
- **Gate**: golden recorded first, every change read; run.sh (box, -O0 and -O2),
  emit.sh, names.sh and overload.sh (box under bash + VS clang), Windows cases
  both spellings, tms6747.sh -O0/-O2, `exclusions --check`, `make comments` 0;
  the deleted refusals' `.error` cases removed in the same commits.
- **Estimate**: 24 h.

### WS-C2 - class declarations, second half (after C1 merges)
- **Repo**: C++Optimize. **Branch** `review/c2-class`, from C1's merged tip.
- **Owns**: the same files as C1 (C1 has finished), plus `include/`? - no:
  `include/` is WS-L's.
- **Items**: F07a, F08, F10, F16, F21, F33, F15 (design note only), D12
  (attributes half).
- **Order inside**: F07a (2 h) → F16 (1 h) → F10 (1 h) → F33 (1 h) → F19
  neighbour check → F08 (2 h) → F21 (4 h) → F15 note (1 h).
- **Gate**: as C1.
- **Estimate**: 14 h.

### WS-D - top level, statements, lexer, preprocessor
- **Repo**: C++Optimize. **Branch** `review/d-toplevel`.
- **Owns**: `src/parser/ParserTopLevel.cpp`, `src/parser/ParserStmt.cpp`,
  `src/Lexer.cpp`, `src/Lexer.h`, `src/Preprocessor.cpp`, `src/Preprocessor.h`,
  `src/parser/Parser.cpp` (the `pending[]`/`implementedElsewhere[]` tables only).
- **Items**: F14, F18, F20, F09, F01, F27a, F27b, F27c, F44, L19 (the
  missing-header refusal table).
- **Order inside**: F20 (1 h) → F09 (1 h) → F44 (0.5 h) → L19 (1 h) → F18 (3 h)
  → F14 (5 h) → F01 (3 h) → F27a (3 h) → F27b (2 h) → F27c (3 h).
- **Note**: F27a's class-iterator loop calls operators through
  `resolveOperator` in ParserOperator.cpp, which it does not edit; if an operator
  path needs a change, the note says so and the main session hands it to E2.
- **Gate**: as C1.
- **Estimate**: 22 h.

### WS-E1 - templates
- **Repo**: C++Optimize. **Branch** `review/e1-templates`.
- **Owns**: `src/parser/ParserTemplate.cpp`, `src/parser/ParserInternal.h`,
  `include/utility` (`std::forward`, `std::move` as the cast they are),
  `src/parser/Parser.h` fields these need (distinct region from C1's; the main
  session resolves a header-only overlap at merge).
- **Items**: F23 (R8), F30, F07b, F31, F34, V3 (the `.nonames` lines F34 lets go).
- **Order inside**: F23 (8 h) → F30 (3 h) → F34 (3 h) → F31 (4 h) → F07b (3 h).
- **Reference collapsing is done in the substitution**, not in `Type.cpp`: when
  the bound type is itself a reference the parameter takes it as it stands, so
  no Type API changes and no file of C1's is touched.
- **Gate**: as C1, plus the Compiler++ build (`tools/c6747-compilerpp` build
  step only, no simulator) and `examples/` building on the box.
- **Estimate**: 22 h.

### WS-E2 - initialisation, expressions, casts
- **Repo**: C++Optimize. **Branch** `review/e2-init-expr`.
- **Owns**: `src/parser/ParserInit.cpp`, `src/parser/ParserExpr.cpp`,
  `src/parser/ParserExprNew.cpp`, `src/parser/ParserExprCall.cpp`,
  `src/parser/ParserExprLambda.cpp`, `src/parser/ParserOperator.cpp`,
  `include/typeinfo` (`std::bad_cast` declaration), `include/initializer_list`.
- **Items**: F24, F25, F03, F22, F29, F41a, F41b, F27d (after D's F27a merge).
- **Order inside**: F29 (3 h) → F24 (8 h) → F25 (3 h) → F03 (1 h) → F22 (4 h) →
  F41a (3 h) → F41b (4 h) → F27d (2 h, rebased on D).
- **Conflict note**: F22 needs `ParserOverload.cpp:1146`'s refusal removed; that
  file is C1's. E2 removes nothing there: it implements the conversion function
  on the closure type so that overload resolution finds it by the ordinary road,
  and the main session deletes the refusal line at merge after C1 (one line,
  said in E2's note).
- **Gate**: as C1; the ledger cases for F24/F27d.
- **Estimate**: 28 h.

### WS-F - the Windows MASM path, the driver, parallel safety
- **Repo**: C++Optimize (and MASM, LINK beds). **Branch** `review/f-windows`.
- **Owns**: `src/backend/Masm.cpp`, `src/backend/Masm.h`, `src/Driver.cpp`,
  `src/Driver.h`, `src/Source.cpp`, `src/Source.h`, `src/optimizer/C6xPipe.cpp`
  (the one line), `tools/windows/run-cases.cmd`, `tools/windows/winlink-check.cmd`,
  `tests/tms6747.sh` (the `CPP11_C6XLIVECHECK` line), `Makefile` `tsan` target
  (a new target; WS-A owns `test` - the two edit different lines of one file and
  the main session merges them: said here so it is not a surprise),
  `MASM/tests/cpp11/*`, `LINK/tests/probes/cpp11-*`, `LINK/tests/known-differ.txt`.
- **Items**: D15/R2, E5, V5, V6, V7, D9/R6, D8 (Driver `-version` zone), D12
  (the success line names a volatile downgrade), P1, P2, P4, V9, R9 (livecheck +
  design note), F38 (design note).
- **Order inside**: reproduce the 10 refusals on the box (1 h) → `.data` segment
  name (1 h) → `$LNleave` duplicate (3 h) → run-cases twice (2 h) → MASM/LINK
  beds (4 h) → `-rts` (3 h) → P1 (3 h) → P2 (0.5 h) → V9 (0.5 h) → D8/D12 (1 h) →
  P4 TSAN on the Linux box (2 h) → notes (1 h).
- **Gate**: Windows cases through *both* spellings with the project's masm and
  LINK (`run-cases.cmd` default now runs both), 0 failed; `winlink-check`;
  MASM's `tests/run.sh` and LINK's `probes.sh`/`bad.sh` on the box; golden 0
  changed outside x86_64-windows MASM files, every MASM change read; run.sh on
  the Linux box with `-j4` under TSAN clean; `make comments` 0.
- **Estimate**: 24 h.

### WS-G - RIDE behaviour, release and seals
- **Repo**: RIDE-4.7 (and `RTS6x/tools/referee --record` flag? no - that is H's).
  **Branch** `review/g-ride`.
- **Owns**: `src/compile.cpp`, `src/toolchain.cpp`, `src/settings.cpp`,
  `src/menu.cpp`, `winforms/` and `macos/` lines for the Run/Emulate menu items
  (Mac-Windows parity rule), `workspace.mk`, `Makefile`, `packaging/release.sh`,
  `packaging/release.cmd`, `tools/master-seal`, `tests/test.cpp`, `tests/test`.
- **Items**: D2/R4, D1/R5 (code), D5, D14, D16/R10, S1, S2, T6 (libs), P3, D13
  (artefact table), A6, A7 (generator), M9.
- **Order inside**: hour 0-3 decisions note for WS-B (`docs/review-2026-10-08/g-decisions.md`)
  → D2 (2 h) → R5 Run/Emulate (6 h, both GUIs, GUI is the oracle: drive RIDE.exe
  on the box through `C:/cxx1/gui` as the memory describes, and the macOS app on
  the Mac is the one exception the user's rules allow for a GUI check) → P3 (2 h)
  → D5 (1 h) → S2 workspace.mk + confirm in release (2 h) → D16 (1 h) → D13
  artefact table + `master-seal --html` (3 h) → D14 (1 h).
- **Gate**: RIDE `tests/test --require-tools` on the box 0 failed, `tests/session`,
  `tests/toolchain-check`; `make confirm` lists no MISSING; `release.cmd` dry run
  refuses on a forced pin mismatch and on a removed `rts6xd.lib`; one tms6747
  project built and *Run* on sim6747 from the GUI on the box and from the macOS
  app; `master-seal check` still passes (source seal unchanged by the artefact
  table).
- **Estimate**: 20 h.

### WS-L - the library
- **Repo**: C++Optimize. **Branch** `review/l-library`, from E1's merged tip.
- **Owns**: `include/*` (except `utility`, `typeinfo`, `initializer_list`, which
  E1/E2 hold until they merge - L starts after E1 and takes `utility` back), new
  headers `include/array`, `include/memory`, `include/functional`, `include/cstdarg`,
  `docs/CONFORMANCE.md` library paragraphs, README.md library paragraph.
- **Items**: F26, L01 (doc), L02, L03, L04, L05, L06, L07, L08, L09 (less/greater
  + note), L10-L17 (notes; L19's table is D's), L18, D12 (setfill, at()).
- **Order inside**: F26, L18, L05, L07 (3 h) → L03, L04 (3 h) → L02 (3 h) → L11
  (2 h) → L06 (2 h) → L08 (5 h) → L09 (1 h) → notes (1 h).
- **Gate**: as C1 (every header change is seen by the cases that include it;
  golden changes read - a header change moves many files), plus Compiler++ and
  the three lambdaTest programs and `examples/` building and running on the
  box; tms6747.sh both legs (RTS6x must still link every case).
- **Estimate**: 20 h.

### WS-H - verification: TI legs, provenance, pins
- **Repos**: C++Optimize (`tools/verify-three`, `tests/names.sh --reasons`,
  `tests/preprocess.sh`, `tests/corpus.sh` baseline, `tools/windows/expected-from-clang.cmd`,
  `docs/VERIFICATION-2026-10-09.md`), RTS6x (`tools/referee --record`,
  `docs/REFEREE-2026-10-09.md`), LNK6x (`src/` unwind index, `tests/known-differ.txt`,
  `docs/known.md`), ASM6x (`tests/compact/known-differ.txt`, README), SIM6747
  (README sentence), VM6747 (`Emulator/README` sentence). **Branch** `review/h-verify`
  in each.
- **Items**: E6, D6, D10, D17/V10, R7, S3/V14, S5/V11, S6, V1, V3, V8, V12, V13
  (doc), V16, F39 (verify), R3 (verify-three line).
- **Scheduled TI-simulator jobs, one at a time on the Windows box, each under
  the 30-minute cap, in this order and at these hours (the plan's only TI
  runs)**:
  1. hour 26: `tools/c6747/release-check` on main as of hour 24 (40 s observed).
  2. hour 27: `RTS6x/tools/referee --record` (both suites, -O2; one DSS session).
  3. hour 29: `tools/c6747-levels` (the 72 kernel builds, one job) - the cycle
     table for the verification page.
  4. hour 31: `tools/c6747-three tools/c6747/programs`.
  5. hour 40 (after all merges): `release-check` again on the merged tree, as
     the final gate's C6747 leg.
  `o2run` (the Compiler++ ten-file workload, ten runs of up to 30 min) is **not**
  run in this plan: it does not fit the cap budget beside the above; the
  verification page says so and names the last recorded result (10-01).
- **Other work**: V16 provenance run (box, VS clang, no simulator) → diff list;
  V1 `preprocess.sh`; D10 lambda cases in the TI build list and the 76 listed;
  LNK6x unwind-index match for CANTUNWIND entries (4 h) and re-pin split by
  region; ASM6x c13 measured and recorded; `verify-three`: `exclusions --check`
  and `make comments` on the Mac step, `tests/test --require-tools` in the RIDE
  leg, `referee` in the c6747 leg; the verification page: beds, sizes, oracles,
  known-differ counts, the four suites' numbers, the TI results of this round.
- **Gate**: LNK6x `tests/run.sh` and `bad.sh` on the box with the new pins
  (every probe at or under its count, none listed that matches); ASM6x
  `check.sh` 14/14 recorded; `preprocess.sh` 0 differ; the provenance diff
  explained line by line; `verify-three` runs end to end once (hour 40-44, the
  final gate).
- **Estimate**: 18 h.

**Total**: 204 agent-hours across ten workstreams, never more than six at once,
inside 48 wall-clock hours with the main session merging in Phase 4.

---

## 5. Gates and merge order

### 5.1 Per-workstream gate (what "done" means before the main session reads a branch)

| Gate | A | B | C1 | C2 | D | E1 | E2 | F | G | L | H |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `tests/run.sh` on the box, -O0 and -O2 (and Linux box -O0/-O2 with g++ build) | . | . | x | x | x | x | x | x | . | x | x |
| `tests/emit.sh`, golden recorded before, every change read and classified in the note | . | . | x | x | x | x | x | x | . | x | . |
| `tests/names.sh` and `tests/overload.sh` on the box (bash + VS clang) | . | . | x | x | x | x | x | . | . | x | x |
| Windows cases, GNU spelling (`run-cases.cmd`) at -O0 and -O2 | x | . | x | x | x | x | x | x | . | x | . |
| Windows cases, MASM spelling with the project's masm + LINK | . | . | after F merges | x | after F | after F | after F | x | . | x | . |
| `tests/tms6747.sh` at -O0 and -O2, both legs (vm6747, sim6747 + RTS6x) | . | . | x | x | x | x | x | x | . | x | . |
| `tools/exclusions --check docs/EXCLUSIONS.md` 0/0 | x | . | x | x | x | x | x | x | . | x | . |
| `make comments` 0 | x | . | x | x | x | x | x | x | . | x | . |
| `tools/seal check` (source seal unchanged until the reseal; a changed sealed file is expected and said) | x | . | x | x | x | x | x | x | . | x | . |
| RIDE `tests/test`, `tests/session`, `toolchain-check` on the box | . | x | . | . | . | . | . | . | x | . | x |
| RTS6x `make check` (vm6747 + sim6747) on the box | . | . | . | . | . | . | . | . | . | x | x |
| MASM/LINK/LNK6x/ASM6x own beds | . | . | . | . | . | . | . | x | . | . | x |
| Handover note `docs/review-2026-10-08/<ws>.md` | x | x | x | x | x | x | x | x | x | x | x |

### 5.2 Merge order (main session; each step: rebase the branch on main, re-run
the branch's gate on the box, merge, push nothing until step 12)

1. **A** (hour 6) - the headline, EXCLUSIONS regenerated, Makefile gate. Every
   later branch rebases on it so its new refusals are checked by `--check`.
2. **B** (hour 6) - RIDE docs; its A3/M1 text waits for G's decision note and
   is amended at step 9.
3. **E1** (hour ~24) - forwarding references first, because L and E2's
   `std::forward` uses depend on it.
4. **C1** (hour ~24).
5. **D** (hour ~22-24).
6. **E2** (hour ~34; rebased on D for F27d; the one `ParserOverload.cpp:1146`
   line removed here, after C1).
7. **F** (hour ~30) - after this, `run-cases.cmd` runs both spellings and every
   later re-gate uses it.
8. **G** (hour ~26, RIDE repo - independent of 3-7).
9. **B amended** for G's decisions (hour ~27).
10. **C2** (hour ~40), **L** (hour ~44), **H** (hour ~42; its RIDE/RTS6x/LNK6x/ASM6x
    parts merged in those repos).
11. **Final gate** (hours 44-47): `tools/verify-three` end to end (Mac step, Linux
    leg, Windows leg both spellings and -O2, c6747 leg = `release-check`, the
    scheduled job 5); `exclusions --check` 0/0; `make comments` 0; RIDE
    `tests/test --require-tools`; RTS6x `make check`; `master-seal check`.
12. **Release steps** (hours 47-48), in this order:
    - re-measure the review's 122-probe grid with `tools/feature-sweep` (WS-A
      writes it from the review's list: one probe per row, clang-validated) and
      record the three counts (compiles / refused by name / refused naming
      nothing - the last must be 0);
    - headline: if F06, F12, F13, F14, F07a, F23 and F24 all landed, the
      banner and the four documents may say "C++11, minus the list in
      docs/EXCLUSIONS.md"; if any did not, they keep "C++03 with C++11
      extensions" and name what is missing - the decision is written in the
      final handover with the grid's numbers;
    - CLAUDE.md: one section per landed feature and fix in the file's style
      (what was measured, what the oracle said, what moved in the golden, what
      is refused), written by the main session from the handover notes; the
      "Reading this tree" order and the "Ordinary C++ this refuses" table
      updated;
    - version bumps: cpp11 **1.7** (`src/Version.h`), reseal `cxx1-1.7.dat` by
      `tools/seal write` on an LF checkout; RIDE **5.2** only if `src/` changed
      (G changes it, so yes) → `ride-5.2.dat`; RTS6x stays 1.2 unless L or H
      changed its `src/` (H changes `tools/` and `docs/` only → unchanged);
      LNK6x **1.2** if H's unwind-index change lands in `src/`; ASM6x stays
      1.0 unless the c13 difference is ours and fixed in `src/`; MASM and LINK
      stay unless their `src/` changed (F adds tests only); VM6747, SIM6747,
      c90, Shalimar, c2s unchanged;
    - `MASTER.SEAL` rewritten by `tools/master-seal` with the new seals and
      the artefact table; the architecture page regenerated from it;
    - sync VM6747's Compiler-Cppi submodule to the new cpp11 main and repin
      (`cpp11-two-checkouts` rule), reseal the pin in MASTER;
    - push every repository's default branch; nothing pushed before this step.

### 5.3 Box usage

Every workstream's builds run in `C:\cxx1\rtsdiv\<ws>` from a clone with
`core.autocrlf=false`; builds and non-simulator tests may run concurrently.
The Linux box runs one leg at a time (`~/ride-5.1/<ws>`), its memory being the
limit (`./build` caps at 300 MB; `-j2`). Only WS-H and the final gate run TI
simulator jobs, in the order of section 4 WS-H.

---

## 6. Risks

- **The Rec-1 features are the plan's size risk.** F06 (a scoped enumeration is
  a new kind of type touching overload ranking and `switch`) and F24 (list-init
  through [over.match.list]) are each a day's work with measurement; if either
  slips past hour 36 it is **merged as far as it is honest** - a refusal by name
  for the forms not reached - and the headline keeps "C++03 with C++11
  extensions". Section 5.2 step 12 is where that is decided, not by an executor.
- **One Windows box for six agents.** Builds are small (cpp11 builds in under a
  minute with `msvc\build.cmd`), but `run-cases.cmd` twice over 628 cases is
  ~15 minutes per gate; agents stagger gates by workspace and must not run
  `tools/c6747-levels` or any DSS job. A DSS job started outside WS-H's schedule
  invalidates both its own and the scheduled job's results (the 10-01 lesson).
- **ParserType.cpp is edited by C1 and then C2** in sequence; if C1 slips, C2
  starts late. Mitigation: C2's items are small and independent; the main
  session may start C2 on main at hour 30 with C1's diff applied as a patch if
  C1 has not merged, and C2 then rebases.
- **`Parser.h` is touched by C1, D, E1, E2** for new fields. Each adds fields in
  a region marked with its workstream name; the main session resolves the
  trivial merges. No two streams change one existing declaration.
- **The emit golden will move a lot** (headers in L, the MASM spelling in F,
  enumeration mangling if a measurement surprises). The rule is unchanged: read
  every change, classify it, and say the counts; a count nobody read is a
  failed gate.
- **names.sh on the Windows box has never run there.** WS-A's first hour either
  proves it (bash + VS clang) or the plan falls back to the Mac for names.sh and
  overload.sh alone, said in WS-A's note and in the final handover as the one
  place the control-room rule was bent.
- **RIDE's Run default change (Rec 5) changes what every user sees on F5**; the
  macOS app and WinForms both change (parity rule), the manual says why, and
  "Emulate" stays one menu item away. A sim6747 run is slower than vm6747's;
  the decision note records the measured difference on `examples/`.
- **LNK6x unwind-index matching** may not reach byte-identity in 4 h; the
  re-pin split by region is the fallback and is itself worth having.
- **The `$LNleave$` duplicate** is known only from the review's run; if the
  reproduction shows a different cause than two funclets sharing a symbol, the
  estimate grows by the difference. The first hour of F is the reproduction.

## 7. What the plan deliberately does not do

- Implement two-phase lookup (F35), `thread_local` (F17), template template
  parameters (F32), non-type packs (F28), UDLs (F02), ref-qualifiers (F36),
  inheriting constructors and class using-declarations (F15), `alignas` on a
  local (F05), `throw(T)` (F11), `goto` out of a handler and function-try-blocks
  (F40), polymorphic virtual bases on x86_64-windows (F37), the Windows EH gaps
  (F38), `<thread>`, `<atomic>`, `<chrono>`, `<regex>`, `<random>`, `<tuple>`,
  `<functional>`'s `std::function`, `<unordered_*>`, a tree-backed `map`/`set`,
  an IR verifier, or the C6000 optimizer on `Mir`. Each has a named refusal or a
  named absence, a live citation, and a design note in the handover of the
  workstream that owns it - which is what rule 8 asks of anything not built.
- Run `o2run` (the Compiler++ ten-file TI workload); the cap budget is spent on
  the five scheduled jobs and the last recorded result is cited.
- Change the seal model: seals stay CRC32 over sources, the user's rule; the
  artefact table is added beside, not instead.
- Touch `tests/c-corpus` beyond a recorded baseline for `corpus.sh`.
- Push anything before the final step, or let an executor merge.

## 8. The first three hours of each workstream

**WS-A**: (1) clone to the box, build, run `tools/exclusions --check` and save
its 116+112 lines; on the box check `"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\x64\bin\clang++.exe"`
exists and run `tests/names.sh` under `"C:\Program Files\Git\bin\bash.exe"`
with that directory first on PATH on three cases - write
`tools/windows/names-overload.cmd` from what worked. (2) Regenerate
EXCLUSIONS.md: `tools/exclusions` output re-cited section by section, the
hand-written entries (throw-type refusals, `L` prefixes) re-checked against a
case each, `typeid`/`operator new` lines deleted, the opening paragraph and
count rewritten; `--check` to 0/0. (3) `tools/version-lines` (reads Version.h,
rewrites the marked lines of README.1ST §6 and README.md's "Known shortcomings
in 1.N" heading) hooked into `tools/seal write` and `check`; Makefile `test`
gains `exclusions --check`; the headline edits in Version.h banner, README.md,
README.1ST §3, EXCLUSIONS.md, CLAUDE.md lines 1-60.

**WS-B**: (1) read every path:line in review §7.3 and quote each as it reads
today into the note; (2) help/07, help/README.md, help/c.md, help/01: four
targets everywhere; help/07:53 and cpp.md:70-82 and 08-debugging.md:43 rewritten
for Debug = clang/CodeView/cdb and Release = RIDE's masm + LINK (or Visual
Studio's when named) - cross-read with `toolchain.cpp:397-400, 705-716, 941-948`;
(3) architecture page: title and banner 5.1, the x86_64-windows row split, the
cpp11→RTS6x and cpp11→lnk6x edges, the vm6747 sentence, the board label, the
backend file list, and a `<!-- seals: generated by tools/master-seal --html -->`
marker where the table will be generated.

**WS-C1**: (1) golden recorded on the box; `override`/`final`: read
`ParserType.cpp:995-1010` and the slot search in ParserClass/ParserVtable; write
the four cases and get clang's answers; (2) implement `override` as a check
after the slot search (refuse "marked override but overrides nothing", clang's
wording in the `.error`), `final` on a member recorded on `VSlot`, `final` on
a class head recorded on `Type` and checked in the base clause; (3) `= default`
/ `= delete`: read `ParserConst.cpp:20-35` and the two doors it serves, design
the one helper, write the cases, measure clang's names for a defaulted
copy constructor (`_ZN1SC2ERKS_` emitted only when used) before coding.

**WS-D**: (1) golden recorded; F20 const member: find the refusal at
`ParserTopLevel.cpp:1050`, read how `: m(args)` stores through `constructMember`,
write `const-member-init.cpp` with clang's output, lift the refusal by building
the store against `unqualified()`; (2) F09 namespace alias and F44 `__COUNTER__`
with their cases; L19's header table in Preprocessor.cpp with its `.error`
case; (3) F18 trailing return: read `ParserTopLevel.cpp:600-630` and how the
parameter list is recorded to be read again, design reading `-> T` in the
parameters' scope, measure clang's mangling of `auto f(int a) -> decltype(a)`
(`_Z1fi` - the return type is not in a free function's name) before coding.

**WS-E1**: (1) golden recorded; read `deduceOne` (`ParserTemplate.cpp:1129-1210`)
and `rankArgument` (`ParserOverload.cpp:570-620`); write `forwarding-reference.cpp`
with clang's output and `names.sh` expectations (`_Z1fIRiEvOT_`, `_Z1fIiEvOT_`
for the lvalue and rvalue calls - measured on the box); (2) implement
[temp.deduct.call]/3: in `deduceOne`, a pattern `T &&` with T unbound and an
lvalue argument binds T to `U &`; in the substitution, a reference to a bound
reference collapses by [dcl.ref]/6 (the parameter takes the bound type itself);
(3) run the forwarding case, then `std::forward` in `<utility>` as
`static_cast<T &&>` with the two overloads, and the `reference-collapsing.cpp`
case for all four pairings.

**WS-E2**: (1) golden recorded; F29: read `ParserExpr.cpp:270-300` and
`anyBaseOffset`; write `static-cast-downcast.cpp` (pointer and reference, null,
two levels, a base at an offset) with clang's output; implement the inverse
offset; (2) F24: read `ParserInit.cpp:900-940`, `constructLocal`, and
`readConstructorInitialiser`; write the five list-init cases and get clang's
answers, including the narrowing `.error`; (3) design [over.match.list] on top of
`resolveOverload`: initializer_list constructors first, then all constructors
with the braces' elements as arguments, `explicit` refused for copy-list-init;
start with the declaration form `P p{1,2}`.

**WS-F**: (1) on the box, build cpp11 and the project's MASM; compile the ten
named cases with `-S -arch x86_64-windows -masm=masm` and assemble each with
`masm.exe /c`; save every message; (2) `.data` segment: `Masm.cpp:471-474` -
name the COMDAT data segment `_DATA` (and `_BSS`, `CONST` for the others)
where the COMDAT form is written, measure what ml64 accepts for the same object
built by cl (`cl /FAs` on a template static), re-assemble the three cases;
(3) the `$LNleave$` duplicate: find which two emissions define
`$LNleave$<fn>$catch$0` for `try-catch.cpp` (`closeFunclet` and whatever else
writes the trailer, or two funclets with one `funcletSymbol_`), fix, re-assemble
the seven cases, link each with LINK and run.

**WS-G**: (1) write `docs/review-2026-10-08/g-decisions.md`: Run (F5) =
`sim6747 --run <program>.out` built by `makeTiProgram`; new menu item "Emulate
on vm6747" with the old behaviour; D2's failure wording; D5's log line; P3's
single-invocation recipe - hand the note to WS-B; (2) D2: `compile.cpp:614` and
`:665-668` return `result.ok = false` with the reason, unless the project's
settings say `emulateOnly`; `tests/test.cpp` gains the three states; (3) begin
R5 in `toolchain.cpp:770-782` (`runRecipe` for tms6747 builds the `.out` first
and runs sim6747), the menu item in `menu.cpp`, the WinForms and macOS entries,
and a GUI check script in `C:/cxx1/gui` for F5 on a C6000 example.

**WS-L** (starts after E1 merges, ~hour 24): (1) golden recorded; `<cstddef>`
`nullptr_t`, `<cstdarg>`, `setfill` in `<iomanip>` + the fill in `ostream`'s
width padding, and the `sto*` family in `<string>` with their cases (clang's
output on the box); (2) `vector`: destroy dropped elements in `erase`,
`pop_back`, `clear` with `p->~T()`, `at()` throwing `out_of_range` under
`#ifndef _WIN32` and aborting with a message under it; the ledger case; (3)
introsort in `<algorithm>` with the large-input case, timing it on the box
against the insertion sort it replaces.

**WS-H** (starts ~hour 24): (1) on the box, `tools/windows/expected-from-clang.cmd`:
every case with an `.expected`, compiled by VS clang with
`-std=c++11 -pedantic-errors`, run, output diffed against the file; save the
list of differences; (2) `tests/preprocess.sh` over the `preprocessor-*` cases
(cpp11 `-E` vs clang `-E -P`, whitespace-normalised) and `tests/names.sh --reasons`;
(3) at hour 26 the first scheduled TI job, `tools/c6747/release-check` on the
main of hour 24, its output saved under `docs/VERIFICATION-2026-10-09/`; then
read `LNK6x/tests/known-differ.txt` and `docs/known.md` and the lnk6x map of
`q07-lib` to split the 31131 bytes by region before touching the linker.
