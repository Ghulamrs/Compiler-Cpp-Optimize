# What cxx1 does not accept

**The language cxx1 accepts is C++03 with C++11 extensions, and this file is
the list of what it does not take.** The library it ships is the one in
`include/` rather than a conforming one. That sentence is why this file exists.
Until 2026-10-08 the headline everywhere in this tree read "C++11 minus this
list", and the review of that day measured it: of 122 probes of C++11 features,
56 were refused - `enum class`, `override`, `= default`, delegating
constructors, alias declarations, forwarding references and list-initialisation
among them. A compiler that refuses those is a C++03 compiler with the C++11
features it has, and a reader who believed the old headline would take a
conforming C++11 program, watch it fail, and have nowhere to look. What is here
of C++11: `auto`, `decltype`, lambdas with every capture, rvalue references
and move, variadic type packs, `static_assert`, `constexpr` functions,
`nullptr`, range-based `for`, `noexcept`, `explicit` conversion functions,
`char16_t`/`char32_t`, `alignas` on a class or a global. The rest of this file
says what is not, each entry citing the line that refuses it.

The tree's own rule 4 says refuse by name and never accept quietly, and rule 5
says a claim with no oracle is not allowed to be believed. This file is those
two rules turned on the top-line claim. It is not hand-written: the inventory
comes out of the source, and `tools/exclusions --check` says when it has
drifted.

## How to re-derive this list

```sh
tools/exclusions                          # every refusal site, file:line and message
tools/exclusions --count                  # the two numbers below
tools/exclusions --check docs/EXCLUSIONS.md
```

**Measured on 2026-10-08 against main `2955c0b`, re-cited the same day: 131
refusal sites, 122 distinct messages.** Every site is cited below, which is what
`--check` verifies - it reports each refusal the source raises and this
document does not cite, each citation whose site is gone, and each hand-written
entry whose test file is gone. Run it after adding or removing a refusal:
`make test` runs it and fails on a non-zero answer, so a document that has to
be remembered is the thing this one was written to replace. The last time it
was left to memory it drifted to 116 uncited and 112 stale in twelve days, and
the review of 2026-10-08 is what said so.

**One grep does not find them.** The messages are written three ways - *"is not
supported yet"*, *"is C++14, and this compiler is C++11"*, and a few that carry
neither and say only *"yet"* - so `tools/exclusions` is the derivation and a
grep is not. **The house rule: a new refusal says "is not supported yet" or
names a standard version**, and is cited here in the same commit.

**A hand-written entry names a test file instead of a source line.** Three
refusals splice their reason in at run time, so the marker word is invisible to
the tool, and a `.notarget` refuses a case for one target with no source line
at all; each of those cites the case that holds it, and `--check` verifies the
file exists. That is as far as a tool can hold a sentence: the case is the
oracle, and the sentence has to be read.

**Ask a one-line program before believing any single line.** A refusal's
*reachability* depends on where it sits. `template <>` at
`src/parser/ParserTemplate.cpp:38` is refused where a template parameter list
is expected, while `template <> struct Box<int> { ... };` compiles; the
functional-cast temporary at `src/parser/ParserOverload.cpp:1071` is refused
during overload ranking, while `take(P(4))` and `P q = P(3);` compile. Both were
checked with a program before this sentence was written, and the same habit is
the reason `pending[]` once had eight keywords in it that were implemented.

---

## The library, which is a subset on purpose

`include/` holds 31 C++ headers and `lib/` 19 C ones; `README.md` lists them
and says what each is honest about. What is **not** there: `<memory>`,
`<functional>`, `<tuple>`, `<array>`, `<unordered_map>`, `<unordered_set>`,
`<list>`, `<deque>`, `<forward_list>`, `<thread>`, `<atomic>`, `<mutex>`,
`<chrono>`, `<regex>`, `<random>`, `<bitset>`, `<complex>`, `<iterator>`,
`<locale>`, `<ratio>`, `<system_error>`, `<typeindex>`, `<cinttypes>`,
`<cstdarg>` (`<stdarg.h>` works), `<cwchar>`, `<cuchar>`, and the rest of the
C++11 library. Inside the headers that exist, whatever a program reaches for
that was not written: `map` and `set` are sorted vectors, `sort` is quadratic,
`vector` drops a slot without destroying it, `at()` does not throw,
`setfill` is accepted and ignored, there is no `emplace_back` and no `stoi`.
A conforming C++ implementation is a compiler **and** a library; cxx1 is a
language translator with four code generators, and every claim about C++11 in
this tree is to be read with that in front of it.

The one *language* refusal the library's absence causes: `auto` from a braced
initialiser deduces an `initializer_list`, and a range-based `for` over a
braced list would need one - `src/parser/ParserTemplate.cpp:1085`,
`src/parser/ParserStmt.cpp:614`.

## Refused because of the standard version

C++11 is the target, so a C++14 or C++17 form is refused *naming the version*
rather than as a missing feature. The standing rule: when a C++11 feature in
this table's neighbourhood is built, the version-boundary refusal beside it
goes in the same commit.

| written | version | site |
| --- | --- | --- |
| `1'000`, a digit separator | C++14 | `src/Lexer.cpp:189` |
| `0b101`, a binary literal | C++14 | `src/Lexer.cpp:319` |
| `u8'x'`, a `u8` character literal | C++17 | `src/Lexer.cpp:390` |
| `decltype(auto)` | C++14 | `src/parser/ParserExpr.cpp:1509` |
| `[n = k]`, an init-capture | C++14 | `src/parser/ParserExprLambda.cpp:231` |
| `auto` as a parameter type | C++14 | `src/parser/ParserClass.cpp:4606`, `src/parser/ParserTopLevel.cpp:698` |
| `auto` as a return type | C++14 | `src/parser/ParserTopLevel.cpp:629` |
| a variable template | C++14 | `src/parser/ParserTemplate.cpp:391` |
| `S s = {1, 2}` where S writes a member initialiser - not an aggregate in C++11 | C++14 changed the rule | `src/parser/ParserInit.cpp:715`, `src/parser/ParserInit.cpp:924`, `src/parser/ParserTopLevel.cpp:379` |
| `static_assert` with no message | C++17 | `src/parser/ParserConst.cpp:196` |
| `namespace N::M { }` | C++17 | `src/parser/ParserTopLevel.cpp:78` |
| an inline variable | C++17 | `src/parser/ParserTopLevel.cpp:230` |

## Lexer and preprocessor

- **a raw string literal**, `R"(...)"`, with any prefix - an ordinary `"..."`
  is a narrow string of char here. `L`, `u`, `U` and `u8` string literals and
  `u`/`U` character literals are read and work (`tests/cases/char16-char32.cpp`,
  `tests/cases/preprocessor-wide.cpp`). `src/Lexer.cpp:393`
- **GNU's named variadic macro parameter** - write `...` and use `__VA_ARGS__`.
  `src/Preprocessor.cpp:942`
- **a user-defined literal**, `operator"" _km` - [lex.ext] wants the literal
  operator looked up by the suffix at every literal, and [over.literal] a
  literal operator template for the raw form; neither path exists.
  `src/parser/ParserType.cpp:2077`, `tests/cases/udl-refused.cpp`

## Templates

Rung 5 landed function and class templates, deduction, partial specialization,
SFINAE and variadic type packs; member function templates and constructor
templates followed. What is left:

- **a template template parameter**, `template <template <class> class C>` -
  a parameter that stands for a template rather than a type needs a third kind
  of binding beside types and values, and the pattern read that spells the
  Itanium name would have to spell it as `TT_`.
  `src/parser/ParserTemplate.cpp:43`, `tests/cases/template-template-parameter-refused.cpp`
- **a non-type parameter pack** - a pack of types is bound to a list of types;
  a pack of values needs a second list beside it and a second expansion.
  `src/parser/ParserTemplate.cpp:63`, `tests/cases/template-variadic-nontype.cpp`
- **an unnamed template parameter** - `src/parser/ParserTemplate.cpp:52`,
  `src/parser/ParserTemplate.cpp:75`
- **a non-type template parameter that is not an integer type** -
  `src/parser/ParserTemplate.cpp:788`
- **`template <>` where a parameter list is expected** -
  `src/parser/ParserTemplate.cpp:38`
- **a template that is neither a class nor a function** -
  `src/parser/ParserTemplate.cpp:311`
- **explicit instantiation**, `template struct Box<int>;` -
  `src/parser/ParserTemplate.cpp:365`; **inside a class** -
  `src/parser/ParserType.cpp:495`. The **declaration**, `extern template`, is
  read and suppresses nothing - `docs/CONFORMANCE.md` says what that costs.
- **an alias template**, `template <class T> using X = ...;` - C++11, and a
  class template with a member typedef says the same thing here.
  `src/parser/ParserTemplate.cpp:376`
- **a constructor or destructor of a class template written outside the
  class** - `src/parser/ParserTemplate.cpp:407`
- **two class templates of one name** - that is partial specialization and is
  written `template <> struct X<...>`; function templates overload.
  `src/parser/ParserTemplate.cpp:521`
- **an explicit specialization of a *function* template** - the class form
  works. `src/parser/ParserTemplate.cpp:538`
- **naming a function template without calling it**, and **instantiating one
  that was only declared** - `src/parser/ParserTemplate.cpp:1828`,
  `src/parser/ParserTemplate.cpp:1881`, `src/parser/ParserTemplate.cpp:1912`
- **naming a member through a class template's argument list**,
  `Box<int>::m` in a declaration - the type is made, so a typedef for it
  reaches the member: `typedef Box<int> B; B::m`.
  `src/parser/ParserTemplate.cpp:1908`, `src/parser/ParserType.cpp:38`
- **a member function template named without being called** -
  `src/parser/ParserTemplate.cpp:1985`; **a member class template** -
  `src/parser/ParserType.cpp:501`; **an out-of-line member template
  definition** - `src/parser/ParserType.cpp:551`; **an `explicit` constructor
  template** - `src/parser/ParserType.cpp:534`
- **`sizeof` of a template parameter in a signature** - the linker name would
  have to spell the expression. `src/parser/ParserExpr.cpp:2452`
- **two-phase name lookup is not done, and that is a decision rather than a
  gap.** A template is instantiated by replaying its tokens, so every name in
  its body is looked up at instantiation - MSVC's old model - and cxx1
  therefore *accepts* a program [temp.res]/9 forbids: a non-dependent name
  declared after the template binds here and is refused by clang. CLAUDE.md
  "Decisions already taken" records why, `docs/CONFORMANCE.md` records the
  over-acceptance, and `tests/open/two-phase-lookup-late-name.cpp` counts it:
  clang refuses it, cxx1 prints 7. A dependent AST is a second parser and a
  second lookup pass, and it is the decision to revisit first if templates ever
  feel wrong.

## Classes, members and friends

- **`= default` outside the class** - a special member defaulted on a later
  declaration is user-provided, where one defaulted inside its class is not.
  `src/parser/ParserConst.cpp:75`
- **a deleted destructor** - every place an object of the class is destroyed
  would have to refuse it, and only calls are checked.
  `src/parser/ParserClass.cpp:138`
- **a deleted virtual function** - its vtable slot would name a function that
  has no body. `src/parser/ParserType.cpp:1082`
- **a delegating constructor** - `src/parser/ParserTopLevel.cpp:1098`
- **a const member named in a mem-initialiser list** -
  `src/parser/ParserTopLevel.cpp:1051`
- **a mem-initialiser that would run a constructor once per element of an
  array member** - `src/parser/ParserTopLevel.cpp:1069`
- **a virtual base initialised with an argument that needs a temporary** - the
  base is built by the most-derived constructor, in a frame the temporary is
  not in; an argument of parameters and constants works.
  `src/parser/ParserTopLevel.cpp:1038`
- **a `constexpr` object of class type** - a `constexpr` constructor runs as an
  ordinary one, and the constant evaluator has no object model to fold the
  object with. `src/parser/ParserConst.cpp:132`; **statements in a `constexpr`
  constructor's body** are C++14. `src/parser/ParserConst.cpp:120`
- **a ref-qualifier**, `f() &` or `f() &&` - the object's value category does
  not choose an overload here. `src/parser/ParserType.cpp:992`,
  `tests/cases/ref-qualifier-refused.cpp`
- **befriending one member function of another class** - `src/parser/ParserType.cpp:709`
- **an anonymous union** - its members would have to become members of the
  class around it, sharing storage. C++98. `src/parser/ParserType.cpp:788`
- **a member function of a union** - `src/parser/ParserClass.cpp:3899`
- **one name holding both a static and a non-static member**, where overload
  resolution picks the non-static one - `src/parser/ParserExpr.cpp:1273`
- **a covariant return that moves the result** - an override may return a
  pointer or reference to a class derived from the base's, and does when that
  base sits at offset 0; where it sits at an offset the result would need a
  thunk at every call through the base. `src/parser/ParserClass.cpp:102`
- **a virtual function of a base after the first with no slot in the class's
  own vtable**, reached through the derived class -
  `src/parser/ParserExpr.cpp:2299`
- **an attribute before a statement**, `[[x]] return 0;` - one before a
  declaration is read wherever [dcl.attr.grammar] places it, C++11's two change
  nothing and an unknown one is ignored (/5); `[[deprecated]]` is C++14.
  `src/parser/ParserType.cpp:1593`, `src/parser/ParserType.cpp:1572`

## Conversion functions and operators

- **`operator->*`** - `src/parser/ParserType.cpp:2075`
- **an operator that can be named but not reached** - `operator&&`,
  `operator||` and `operator,`: refused at the declaration, because a function
  that links and can never be called is the half-built thing this project
  refuses everywhere. Every other overloadable operator resolves from an
  expression, asked of a one-line program each: `+ - * / % & | ^ << >> == != <
  <= > >=` binary, `+ - * & ! ~ ++ --` unary, `() [] = ->`, the ten compound
  assignments, and `operator new`, `operator delete` and the placement forms.
  `src/parser/ParserType.cpp:2225`

## Initialisation, and braces

- **list-initialisation calling a constructor**, `P p{1, 2}` - write the
  arguments in parentheses. Two shapes *are* read: the empty pair, `{}`, which
  is value-initialisation; and a braced list handed to a class that declares a
  `std::initializer_list` constructor. `src/parser/ParserInit.cpp:932`,
  `src/parser/ParserInit.cpp:46`
- **`T{...}` with a value in the braces** as an expression - the empty pair is
  read: `T{}` value-initialises, as `T()` does. `src/parser/ParserExpr.cpp:490`
- **`T{}` on a class in an expression** - `T()` does the same and is read.
  `src/parser/ParserExpr.cpp:496`
- **`S{...}` as an expression** - `S(...)` calls a constructor, and a plain
  struct is built by naming its members. `src/parser/ParserExpr.cpp:1442`
- **`new T{...}`** with a value inside - `new T{}` value-initialises.
  `src/parser/ParserExprNew.cpp:1133`
- **a braced default argument** - `src/parser/ParserClass.cpp:4620`,
  `src/parser/ParserTopLevel.cpp:751`
- **a braced member initialiser with values on a class, an array or a
  reference** - a scalar member takes `{v}` and `= {v}`, and `= {}` and `= {0}`
  zero any member. `src/parser/ParserType.cpp:1152`
- **a braced list for an array of a class with a destructor and no
  constructor** - `src/parser/ParserInit.cpp:1189`,
  `src/parser/ParserStmt.cpp:167`
- **a bit-field initialised at file scope** - `src/parser/ParserInit.cpp:681`
- **`auto` from a braced initialiser** - it deduces an `initializer_list`.
  `src/parser/ParserTemplate.cpp:1085`

## Objects that would run code before `main`

Dynamic initialisation runs - a file-scope object with a constructor, a static
data member of one, a static local with one, a reference at either scope, and a
scalar whose initialiser does not fold. What is left is where one object is
many, or has no object of its own:

- **a static data member that is an array of a class with a constructor** -
  each element would need its constructor before main and its destructor at
  exit; a file-scope array and a static local of the same shape are built and
  destroyed element by element. `src/parser/ParserClass.cpp:3734`
- **a static-duration reference bound to a temporary** - [class.temporary]/5
  gives the temporary the program's lifetime, so it would need static storage
  of its own; a named object binds. `src/parser/ParserInit.cpp:1589`

## Expressions

- **`static_cast` of a reference to a different type** -
  `src/parser/ParserExpr.cpp:287`
- **a name qualified with `::` alone in an *expression*** - as a *type*,
  `::Lexer *p;` works; a name in an expression goes through the namespace and
  using-directive lookup, and restricting that for one name is a flag that has
  to be put down again before the call's arguments are parsed.
  `src/parser/ParserExpr.cpp:960`
- **choosing an overload by the type it is assigned to** -
  `src/parser/ParserExpr.cpp:552`
- **a pointer to a *const* member function** - the constness of `this` is not
  part of a function type here. `src/parser/ParserType.cpp:2313`
- **postfix `++` / `--` on a bit-field** - the prefix form works.
  `src/parser/ParserOperator.cpp:678`
- **`va_arg` of an aggregate** - `src/parser/ParserExpr.cpp:747`
- **an `auto` variable that names itself in its own initialiser** - its type is
  what the initialiser decides, so there is none yet; [dcl.spec.auto]/3, and
  clang refuses it too. `src/parser/ParserTemplate.cpp:1077`

## `new` and `delete`

- **more than one value in a new-expression** without a constructor -
  `src/parser/ParserExprNew.cpp:1159`
- **`new T[n][m]`** - only the first dimension may be given.
  `src/parser/ParserExprNew.cpp:1107`

Plain `new` and `delete`, a class's own `operator new`/`operator delete`, the
placement forms and `<new>` all work; the entries that said otherwise were
stale by 2026-10-08 and are gone.

## Run-time type information and exception types

A class carries a run-time description on every target - `_ZTI` and `_ZTS`
behind the vtable on Itanium, including `__vmi_class_type_info` for a class
with more than one base; five `??_R` records and a locator in front of the
vftable on Microsoft - and `dynamic_cast` to a pointer, `dynamic_cast<void *>`
and `typeid` work (`tests/cases/typeid.cpp`). What is left:

- **`dynamic_cast` to a reference** - it has no null to answer with, so a
  failure throws `std::bad_cast`, which needs a class-typed throw on every
  target. `src/parser/ParserExprNew.cpp:660`
- **`dynamic_cast` naming a class with more than one base** - the type_info is
  emitted; the cast's runtime walk over it is not. `src/parser/ParserExprNew.cpp:720`
- **catching a pointer by reference** - the runtime hands a handler the pointer
  itself, so catch it by value. `src/parser/ParserStmt.cpp:1308`
- **throwing a pointer to a function** - it has no `type_info`; the message
  splices its reason in at run time, so this entry is hand-written.
  `tests/cases/throw-refused.cpp`
- **a terminate scope on x86_64-windows** - not a refusal but an acceptance
  the standard forbids, recorded here because a reader of this list will meet
  it: the FH3 tables cxx1 writes mark no funclet as terminating, so a destructor
  that throws while an exception unwinds, a `catch` parameter whose copy throws,
  and a `noexcept` function whose callee throws are all caught by the next
  handler where [except.terminate] says `std::terminate`. The Itanium targets
  terminate. `tests/cases/dtor-throws-unwinding.notarget`,
  `tests/cases/catch-copy-throws.notarget`,
  `tests/cases/noexcept-terminates-callee.notarget`
- **a thrown pointer caught by a pointer to a base at an offset, on tms6747** -
  TI's `rts6740` hands it over unadjusted and cxx1's type_info is cl6x's word
  for word, so the case holds TI's measured answer rather than clang's.
  `tests/cases/throw-pointer-base-adjust.notarget`

## Statements, exceptions and control

- **a local with a destructor and a `try` in one function** - refused only
  where the call-site table cannot be split; beside the `try`, inside its body
  and inside a handler all work on the Itanium targets.
  `src/parser/ParserStmt.cpp:858`, `src/parser/ParserStmt.cpp:2024`
- **a `goto` that leaves a `catch` handler** - [except.handle]/16 ends the
  handling on the way out, and the call that ends it has to be emitted where
  the jump is written; a forward label has not been read there. `return`,
  `break` and `continue` out of a handler work, and so does a `goto` to a label
  inside it. `src/parser/ParserStmt.cpp:2111`,
  `tests/cases/goto-out-of-handler-refused.cpp`
- **a function try block**, `int f() try { } catch (...) { }` - its handler
  covers the mem-initialisers as well as the body, so a `try` inside the body
  is not the same thing. C++98. `src/parser/ParserTopLevel.cpp:1236`,
  `tests/cases/function-try-block-refused.cpp`
- **a dynamic exception specification**, `throw(T)` - deprecated in C++11; it
  needs a run-time check of the thrown type against a list, where `noexcept` is
  a promise the compiler only records. `throw()` with nothing in it is read as
  `noexcept`. `src/parser/ParserConst.cpp:237`,
  `tests/cases/noexcept-dynamic-spec-refused.cpp`
- **a class declared in the condition of a loop** - [stmt.iter]/2 builds it
  afresh on every turn, and only a scalar can be written where the test is; a
  class in the condition of an `if` works. `src/parser/ParserStmt.cpp:799`
- **a range-based `for` over a temporary** - the range is held as a pointer
  here and `auto &&` would have extended the temporary's life; name it first.
  `src/parser/ParserStmt.cpp:528`
- **a range-based `for` over a class with no member `begin`/`end`** - the free
  functions [stmt.ranged]/1 falls back to are not looked for; the member form
  is what every container in `include/` provides. `src/parser/ParserStmt.cpp:535`
- **a range-based `for` whose iterator is a class** - every `begin()` in
  `include/` returns a pointer and the loop built is the pointer loop.
  `src/parser/ParserStmt.cpp:570`
- **a range-based `for` over a braced list** - `src/parser/ParserStmt.cpp:614`
- **an rvalue reference as the loop variable of a range-based `for`** -
  `src/parser/ParserStmt.cpp:623`
- **a trailing return type**, `auto f(int) -> int` - C++11, refused as the
  feature it is rather than as `auto` deduction. `src/parser/ParserTopLevel.cpp:624`

## Namespaces and lookup

- **a namespace alias**, `namespace A = N;` - `src/parser/ParserTopLevel.cpp:81`
- **an inline namespace** - its members would have to be found in the
  namespace around it. `src/parser/ParserType.cpp:1735`
- **a using-declaration inside a class**, `using B::f;` - it redeclares a base
  member, changing its access and joining the overload set; an inheriting
  constructor is the same declaration one step on. `src/parser/ParserType.cpp:566`
- **a using-declaration inside a block** - `src/parser/ParserStmt.cpp:1722`.
  The one at namespace scope works, and so does `using namespace N;`.

## Lambdas

- **naming a capture after a default one** - `[=]` and `[&]` on their own take
  everything the body reads, `this` included. `src/parser/ParserExprLambda.cpp:198`
- **a capture-less lambda converting to a function pointer** -
  [expr.prim.lambda]/6's conversion function is not synthesised.
  `src/parser/ParserOverload.cpp:1201`

## `volatile`, which is read and dropped

cxx1 accepts `volatile` on an object and keeps nothing of it in the type
system; on an object that costs nothing, since every read is a load, and those
shapes are accepted. Where the qualifier would reach a linkage name it is
refused (CLAUDE.md, "The volatile sweep"):

| refused | measured against | site |
| --- | --- | --- |
| a pointer or reference to a volatile type | clang `_Z1fPVi`, cl `PECH` | `src/parser/ParserType.cpp:1657` |
| `T *volatile` | cl `REAH`; Itanium right by accident | `src/parser/ParserType.cpp:1630` |
| a volatile member function | the cv on `this` is in the name on both ABIs | `src/parser/ParserType.cpp:1003` |
| a typedef of a volatile type | it would launder the qualifier past the above | `src/parser/ParserType.cpp:1664` |

## Keywords the parser has no rule for

Three, from `pending[]` in `src/parser/Parser.cpp`, each refused **by name** at
the three doors a keyword can arrive at - a name, an expression, a declaration:
`src/parser/Parser.cpp:101`, `src/parser/ParserExpr.cpp:679`,
`src/parser/ParserType.cpp:1950`; `tests/cases/refused.cpp` holds one.

    asm   export   thread_local

`thread_local` stays: thread-local storage is a different model on each
target - TLS relocations and `__tls_get_addr` on ELF, `__tlv_bootstrap` on
Mach-O, the TLS directory and `_tls_index` on COFF, and nothing at all on the
C6000 - and a keyword implemented on three targets and refused on the fourth is
not the shape this compiler takes. `alignas`, `alignof` and `typeid` left the
list by being implemented; `alignas` on a *local* past the stack's alignment is
still refused, by name, since it would need the frame realigned -
`src/parser/Parser.cpp:831`, `tests/cases/alignas-local-past-stack-refused.cpp`.

A second list beside it, `implementedElsewhere[]`, answers *"it is implemented,
but it does not begin an expression"* for the keywords that are implemented and
cannot stand there. **Measure with a one-line program before moving a name
between the two lists.**

## Refused for one target only

- **a polymorphic base that is not the first, on the Microsoft ABI** - cl lays
  the base with the vfptr first wherever it was written and compiles an
  override against a biased `this` where Itanium puts a thunk in front.
  `src/parser/ParserClass.cpp:1790`, `src/parser/ParserType.cpp:300`
- **a virtual base with virtual functions, on x86_64-windows** - cl dispatches
  through one with vtordisp fields and thunks of its own, measured in CLAUDE.md
  "The Microsoft virtual-base layout" and not built. `src/parser/ParserType.cpp:442`
- **a pointer to a member of an incomplete class, on x86_64-windows** - the
  Microsoft ABI sizes one by the class's inheritance model, and an incomplete
  class takes cl's widest form. `src/parser/ParserType.cpp:2399`
- **a `volatile` object with external linkage, on x86_64-windows** - cl
  decorates its name with the qualifier, `?g@@3HC` for `?g@@3HA`.
  `src/parser/ParserType.cpp:1642`
- **a `try` inside a `catch` handler, on x86_64-windows** - a handler is a
  funclet there, and a nested `try`'s handlers would be funclets inside it.
  `src/parser/ParserStmt.cpp:1193`
- **a local with a destructor inside a `catch` handler, on x86_64-windows** -
  the same reason, a cleanup being a funclet. `src/parser/ParserStmt.cpp:1151`
- **a rethrow, `throw;`, on x86_64-windows** - `_CxxThrowException` with two
  null pointers from inside a funclet, not measured on the box.
  `src/parser/ParserStmt.cpp:1743`
- **a member under `#pragma pack`, on tms6747** - the C6000 backend loads words
  aligned only, and cl6x has no `#pragma pack`. `src/parser/ParserType.cpp:58`

A case that cannot be compiled for a target names it in `<case>.notarget` with
the reason on the line, which is printed on every run.

## Three the tool finds that are not exclusions

`tools/exclusions` cannot tell a refusal from an ordinary diagnostic that
happens to say "yet", and filtering them inside the script would hide a
judgement in a program. They are named here instead:

- `src/parser/ParserType.cpp:214` - a base class that is not yet defined. An
  ordinary error: a derived object contains its base, so the base has to be
  complete.
- `src/Mangle.cpp:617`, `src/Mangle.cpp:1169` - a type with no Itanium or
  Microsoft linkage name. Internal: reaching either means a type was built that
  the mangler was never taught, which is a bug in this compiler and not a
  statement about the language.

## The rule this file leaves behind

**A refusal and a claim are the same kind of thing, and both need an oracle.**
A refusal that names a feature the compiler now has is worse than none, because
it is a claim the compiler cannot support - that is why `pending[]` was
measured keyword by keyword, and why the `typeid` and `operator new` entries
that stood here until 2026-10-08 were a defect. A *headline* the compiler
cannot support is the same defect one level up, which is what this document
fixes. When a feature lands, delete its entry here and run
`tools/exclusions --check`; when a refusal is added, cite it here in the same
commit; and `make test` fails until both are true.
