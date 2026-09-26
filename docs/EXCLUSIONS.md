# What cxx1 does not accept

**The language cxx1 accepts is C++11 minus this list, and the library it
ships is the one in `include/` rather than a conforming one.** That sentence is
why this file exists. `CLAUDE.md`
opens by saying the language cxx1 compiles is C++11, which is the right
headline and was, on its own, a claim the compiler cannot support: a C++11
compiler that refuses `dynamic_cast` is a C++11 *subset*, and a reader who
believed the headline would take a conforming C++98 program, watch it fail,
and have nowhere to look.

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

**Measured after `556e643`, re-cited on 2026-09-26: 130 refusal sites, 121 distinct messages.** Every site
is cited below, which is what `--check` verifies — it reports each refusal the
source raises and this document does not cite, and each citation whose site is
gone. Run it after adding or removing a refusal; a document that has to be
remembered is the thing this one was written to replace.

**One grep does not find them, and that is worth fixing.** Compiler++'s
`KNOWN-GAPS.md` can say `grep -h 'not supported in this version'` and be
complete, because all forty of its sites use that one phrase. Here the
messages are written three ways — *"is not supported yet"*, *"is C++14, and
this compiler is C++11"*, and eleven that carry neither and say only *"yet"*.
`dynamic_cast`'s message is in the third group, so the most consequential
exclusion in the compiler is the one a naive grep misses. **The house rule
worth adopting: a new refusal says "is not supported yet" or names a standard
version.** Until every message does, `tools/exclusions` is the derivation and
a grep is not.

**Ask a one-line program before believing any single line.** A refusal's
*reachability* depends on where it sits, and three of these read more broadly
than they fire. `template <>` at `src/parser/ParserTemplate.cpp:38` is refused
where a template parameter list is expected, while
`template <> struct Box<int> { … };` compiles; the functional-cast temporary at
`src/parser/ParserOverload.cpp:945` is refused during overload ranking, while
`take(P(4))` and `P q = P(3);` compile. Both were checked with a program before
this sentence was written, and the same habit is the reason `pending[]` had
eight keywords in it that were implemented.

---

## The library, which is no longer nothing

**`include/` holds the C++ headers this compiler ships**, and `lib/` holds the
sixteen C headers underneath them. What is there: `<cstddef>`, `<cstdlib>`,
`<cstring>`, `<cmath>` and `<cctype>`, each its C header wrapped by
using-declarations into `std`, with every name that header declares; and
`<string>`, which is a class.

`<utility>`, `<vector>`, `<map>`, `<set>` and `<algorithm>` are there too: a
vector is a growing array, a map is a sorted vector of pairs, a set is a sorted
vector, and an iterator in all of them is a pointer.

`<iostream>`, `<ostream>`, `<istream>`, `<sstream>`, `<fstream>`, `<ios>` and
`<cstdio>` are there now. Two things made them possible and one had to be
fixed. **`std::cout` is an object at file scope with no constructor** - an
aggregate with a constant initialiser, whose `FILE *` is resolved at the point
of use - because a file-scope object with a constructor would have to run one
before `main`, which is refused below. **`if (stream >> v)` is a conversion
function**, which is why these could not have been written before those landed.
And a **reference to a base would not bind to a derived object**, so
`std::getline(istringstream, s)` found no matching function; the pointer form
had always worked and the reference form was never done.

A stringstream reuses its base's operators through one pointer - `ostream::buf_`
and `istream::src_` - rather than a virtual, because a virtual needs a vptr and
a vptr needs the constructor `cout` cannot have.

What the streams do not have: **`rdbuf()` and the `streambuf` layer under it**,
so `ss << in.rdbuf()` - the idiom for slurping a file - has no meaning here;
read with `getline` or `ifstream::readAll`. There are no format flags either,
so no `setw`, no `setprecision`, and no `boolalpha` - a `bool` prints as `1`,
which is what the default is anyway.

A converting constructor is called to make an argument now - [over.ics.user] -
so `m["key"]` and `f(literal)` against a `const std::string &` parameter work,
and the containers are written the way anyone writes them. What that rule does
*not* do is chain: at most one user-defined conversion per sequence, so nothing
makes a `Far` out of an `int` because a `Near` sits between them.

One limit the headers themselves carry, a compiler limit rather than a choice.
**`<cmath>` overloads nothing**: `lib/`
declares the `double` form alone, so `std::sqrt(2.0f)` converts to double and
back rather than calling `sqrtf`. The answer is right; the call is not the one a
conforming library makes.

The C headers underneath, unchanged and usable on their own: `assert.h`, `ctype.h`, `errno.h`, `float.h`, `limits.h`,
`locale.h`, `math.h`, `memory.h`, `setjmp.h`, `signal.h`, `stdarg.h`,
`stddef.h`, `stdio.h`, `stdlib.h`, `string.h`, `time.h`.

So what is still missing is `<memory>`, `<exception>`,
`<stdexcept>`, `<list>`, `<deque>`, `<iterator>`, `<limits>` and
the rest - and inside the headers that do exist, whatever a program reaches for
that was not written. This is a library sized to what has been asked of it
rather than to the standard, and the distance between those two is not small.
It is also why a *language* feature is refused in one place: `auto` from a
braced initialiser deduces an `initializer_list`, which there is no library for
— `src/parser/ParserTemplate.cpp:1061`.

A conforming C++ implementation is a compiler **and** a library. cxx1 is a
language translator with three code generators. Read every claim about C++11
in this tree with that in front of it.

## Run-time type information

A class carries a run-time description on all three targets now — `_ZTI` and
`_ZTS` behind the vtable on Itanium, five `??_R` records and a locator in front
of the vftable on Microsoft — and `dynamic_cast` to a pointer works on each.
`dynamic_cast<void *>`, the most-derived-object form, works on each too, and
reads none of that: Itanium takes offset-to-top from in front of the vtable
inline, Microsoft calls `__RTCastToVoid`. What is left of it:

- **`dynamic_cast` to a reference** — it has no null to answer with, so a
  failure throws `std::bad_cast`, and there is no C++ standard library here to
  throw it from. `src/parser/ParserExprNew.cpp:644`
- **`dynamic_cast` naming a class with more than one base** — that wants
  `__vmi_class_type_info`, a third shape carrying the bases' offsets and flags.
  Such a class still compiles and its vtable still works; only the cast is
  refused. `src/parser/ParserExprNew.cpp:704`
- **`typeid`** — in the keyword table below. Nothing emits a `type_info` for a
  *fundamental* type either; a class's is what landed.

**A class can be thrown and caught on the two Itanium targets**, by value, by
reference and by a base — `emitClassTypeInfo` needs no vtable, so a plain class
is as throwable as a polymorphic one, and `__si_class_type_info` carries the
base chain the runtime walks. Three things about that are still refused, and
`tools/exclusions` cannot derive any of them: the message is
`"'throw' cannot name the type of this: " + why`, and the word that marks a
refusal arrives at run time inside `why`. So they are written out here instead.

- **throwing or catching a pointer** — that wants `__pointer_type_info`, a
  third shape beside `__class_type_info` and `__si_class_type_info`, carrying
  the pointee's own object and the qualifiers written with it.
  `tests/cases/throw-refused.cpp`
- **throwing or catching a class with more than one base** — the same
  `__vmi_class_type_info` the `dynamic_cast` entry above wants.
  `tests/cases/throw-multiple-bases-refused.cpp`
- **a class-typed throw on x86_64-windows** — the ThrowInfo chain has to point
  at a class: a type descriptor and a catchable type per class, and a `_CTA`
  array listing the thrown class *and its bases*, which is how catch-by-base
  works on that ABI. Measured from clang for that target, throwing a `Derived`
  emits `??_R0?AUDerived@@@8` and `??_R0?AUBase@@@8`, a `_CT??_R0?AU...@84`
  for each, `_CTA2?AUDerived@@` and `_TI2?AUDerived@@`. cxx1 emits the four
  objects for a fundamental type only. `tests/cases/throw-class.notarget`

## Templates

Rung 5 landed function and class templates, deduction, partial specialization,
SFINAE and variadic packs. What is left:

- **a template template parameter** — `src/parser/ParserTemplate.cpp:43`
- **a member function template beyond an explicit-argument call** — a member
  function template is supported and called with explicit arguments,
  `v.head<3>()`, on all three targets. Microsoft mangling is exact; on Itanium the
  name encodes the *substituted* signature where clang uses the *pattern* (a
  template parameter as T_), so a member-template name diverges whenever its
  signature mentions a parameter - the same pattern-vs-substituted split free
  function templates already make, not yet made here for members. Recorded in
  template-member.nonames. What is refused: a **member class template** —
  `src/parser/ParserType.cpp:499`; an **out-of-line member template
  definition**, one written outside its class —
  `src/parser/ParserType.cpp:540`; **explicit instantiation inside a class** —
  `src/parser/ParserType.cpp:493`; and a member function template **named
  without being called** — `src/parser/ParserTemplate.cpp:1895`. A member
  template whose arguments would have to be **deduced** from the call, rather
  than written, is not reached: the call finds no such member and says so.
- **an unnamed template parameter** — `src/parser/ParserTemplate.cpp:52`,
  `src/parser/ParserTemplate.cpp:75`
- **a non-type parameter pack** — a pack of types is supported.
  `src/parser/ParserTemplate.cpp:63`
- **a non-type template parameter that is not an integer type** —
  `src/parser/ParserTemplate.cpp:777`
- **two class templates of one name** — function templates overload (a call
  deduces against each and overload resolution ranks the specializations), but
  two *class* templates of one name is partial specialization, a different
  path. `src/parser/ParserTemplate.cpp:510`
- **an explicit specialization of a *function* template** — the class form
  works. `src/parser/ParserTemplate.cpp:527`
- **`template <>` where a parameter list is expected** —
  `src/parser/ParserTemplate.cpp:38`
- **an alias template**, `template <class T> using X = ...;` — a class
  template with a member typedef says the same thing here.
  `src/parser/ParserTemplate.cpp:365`
- **explicit instantiation** — `src/parser/ParserTemplate.cpp:354`
- **a template that is neither a class nor a function** —
  `src/parser/ParserTemplate.cpp:304`
- **a constructor or destructor of a class template written outside the class**
  — `src/parser/ParserTemplate.cpp:396`
- **naming a function template without calling it** —
  `src/parser/ParserTemplate.cpp:1749`, `src/parser/ParserTemplate.cpp:1749`
- **instantiating a template that was only declared** —
  `src/parser/ParserTemplate.cpp:1749`
- **`sizeof` of a template parameter in a signature** — the linker name would
  have to spell the expression. `src/parser/ParserExpr.cpp:2314`
- **an explicit instantiation declaration**, `extern template` — a
  specialization is emitted wherever it is used here, so there is nothing to
  suppress. `src/parser/ParserType.cpp:1575`

## Classes, members and friends

- ~~a virtual base~~ — **no longer refused**, and this entry was stale.
  `struct D : virtual B` compiles, and `tests/cases/virtual-base-diamond`
  measures that the base is constructed once by the most-derived class on both
  Itanium targets (x86_64-windows keeps a named refusal, `.notarget`). Reading
  its mem-initialiser is honoured too, since 2026-09-08 - see
  docs/CONFORMANCE.md for what was wrong before it.
- **one name holding both a static and a non-static member**, where overload
  resolution picks the non-static one — the arguments have been read by then,
  and there is no honest way back to the call that takes an object.
  `src/parser/ParserExpr.cpp:1231`. A static member function on its own works.
- **a member function of a union** — `src/parser/ParserClass.cpp:3342`
- **`friend class X;`** — one named function can be befriended.
  `src/parser/ParserType.cpp:672`
- **befriending one member function of another class** —
  `src/parser/ParserType.cpp:684`
- **a const member named in a mem-initialiser list** —
  `src/parser/ParserTopLevel.cpp:980`
- **a delegating constructor** — `src/parser/ParserTopLevel.cpp:1027`
- **a scoped enumeration**, `enum class` — an enumeration is an int that
  remembers its name here, where a scoped one is a distinct type whose
  enumerators are reached through it. `src/parser/ParserType.cpp:1374`
- **`= default` and `= delete`** — a defaulted member is written with an empty
  body here, and a deleted one by declaring it private and never defining it.
  A constructor and a member function reach that position by different doors,
  so one helper answers for both. `src/parser/ParserConst.cpp:26`
- **`override` and `final` on a member function** — an override is found by its
  base's slot whether or not the word is written, so this would be a check
  rather than a change. `src/parser/ParserType.cpp:977`
- **`final` on a class** — nothing records that a class may not be derived
  from. `src/parser/ParserType.cpp:83`
- **a ref-qualifier**, `f() &` or `f() &&` — the object's value category does
  not choose an overload here. `src/parser/ParserType.cpp:966`
- **a `constexpr` constructor** — the constant evaluator folds a call to a
  function and has no object to build. `src/parser/ParserType.cpp:577`
- **an anonymous union** — its members would have to become members of the
  class around it, sharing storage. C++98, not C++11.
  `src/parser/ParserType.cpp:763`

## Conversion functions and operators

- **`operator->*`** — `src/parser/ParserType.cpp:1936`
- **a user-defined literal** — `src/parser/ParserType.cpp:1938`
- **`operator&&`, `operator||`, `operator,` and `operator->*`** — the four that
  still fall into the generic refusal below, **named** rather than left to it.
  The ten compound assignments used to be here too and are reachable now; a
  class that declares `operator+` and `operator=` but not `operator+=` is still
  refused `+=`, because [over.ass] makes each `@=` an operator of its own and
  the rewrite into `+` and an assignment is for built-in operands only.
  A bullet that says only "an operator that can be named but not reached" hid
  `operator[]`, `operator=` and `operator->` for as long as it stood, which
  meant the document could not answer "can a container be written here?" - and
  the answer was no. Those three are reachable now; these four are what is
  left, and none is needed to write one. The first two would want the
  short-circuit to stop short-circuiting, which is the whole of why they are
  rarely overloaded.
- **an operator that can be named but not reached** — the rule the four above
  are refused by: refused at the declaration, because a function that links and
  can never be called is the half-built thing this project refuses everywhere.
  Every other overloadable operator resolves from an expression, asked of a
  one-line program each: `+ - * / % & | ^ << >> == != < <= > >=` binary,
  `+ - * & ! ~ ++ --` unary, and `() [] = ->`.
  `src/parser/ParserType.cpp:2086`

## Initialisation, and braces

- **list-initialisation calling a constructor**, `P p{1, 2}` — write the
  arguments in parentheses. Two shapes *are* read: the *empty* pair, `{}`,
  which is value-initialisation; and a braced list handed to a class that
  declares a `std::initializer_list` constructor, which is built through it.
  `src/parser/ParserInit.cpp:899`, `src/parser/ParserInit.cpp:39`
- **`T{...}` with a value in the braces** — list-initialisation written as an
  expression; write the value in parentheses. The empty pair is read: `T{}`
  value-initialises, as `T()` does. `src/parser/ParserExpr.cpp:470`
- **`T{}` on a class** — value-initialising a class with braces *in an
  expression*; `T()` does the same and is read.
  `src/parser/ParserExpr.cpp:476`
- **an inline variable** — `inline` on a variable is a C++17 feature; C++11
  has `inline` only on functions, where it works. `src/parser/ParserTopLevel.cpp:210`
- **a braced default argument** — `src/parser/ParserClass.cpp:4019`,
  `src/parser/ParserTopLevel.cpp:705`
- **a braced member initialiser** — `src/parser/ParserType.cpp:1124`
- **an initialiser for an array of a class** —
  `src/parser/ParserStmt.cpp:159`, `src/parser/ParserTopLevel.cpp:998`
- **a bit-field initialised at file scope** — `src/parser/ParserInit.cpp:645`
- **`S{...}` as an expression** — list-initialisation one syntax over from the
  declaration form. `S(...)` calls a constructor, and a plain struct is built
  by naming its members. `src/parser/ParserExpr.cpp:1398`

## Objects that would run code before `main`

Dynamic initialisation runs now - [basic.start.init]/2 in the file's own
`_GLOBAL__sub_I_<file>`, [stmt.dcl]/4 under the ABI's guard, and
[basic.start.term] through `__cxa_atexit` or `atexit` - so a file-scope
object with a constructor, a static data member of one, a static local with
one, and a reference at either scope all work - and, since 2026-09-19, a
scalar or trivial class whose initialiser does not fold (`int g = init(1);`,
`P g = P();`, `T Tm<T>::st = T();`), stored by the same function in
declaration order. What is left is the shapes where one object is many:

- **a static data member that is an array of a class with a constructor** —
  each element would need its constructor before main and its destructor
  at exit; a file-scope array and a static local of the same shape are
  built and destroyed element by element since 2026-09-19.
  `src/parser/ParserClass.cpp:3235`
- **a static-duration reference bound to a temporary** — [class.temporary]/5
  gives the temporary the program's lifetime, so it would need static storage
  of its own; a named object binds. `src/parser/ParserInit.cpp:1500`

## Expressions

- **`static_cast` of a reference to a different type** —
  `src/parser/ParserExpr.cpp:276`
- **a name qualified with `::` alone in an *expression*** — as a *type* it
  works: `::Lexer *p;` names the global scope past a nearer class or
  namespace, which is one look in one table. A name in an expression goes
  through `qualifyForLookup`, where a namespace and a using-directive get
  their say, and restricting that for one name is a flag that has to be put
  down again before the call's arguments are parsed.
  `src/parser/ParserExpr.cpp:938`
- **choosing an overload by the type it is assigned to** —
  `src/parser/ParserExpr.cpp:532`
- **a pointer to a *const* member function** — the constness of `this` is not
  part of a function type here. `src/parser/ParserType.cpp:2168`
- **postfix `++` / `--` on a bit-field** — the prefix form works.
  `src/parser/ParserOperator.cpp:674`
- **`va_arg` of an aggregate** — `src/parser/ParserExpr.cpp:728`
- **a functional-cast temporary reached through overload ranking** — a
  converting constructor is not tried at a call.
  `src/parser/ParserOverload.cpp:945`
- **naming a non-static data member with no object**, `sizeof(S::m)` —
  [expr.sizeof]/2. Told apart from a name the class does not have at all.
  `src/parser/ParserType.cpp:32`

## `new` and `delete`

- **more than one value in a new-expression** — `src/parser/ParserExprNew.cpp:1143`
- **`new T[n][m]`** — only the first dimension may be given.
  `src/parser/ParserExprNew.cpp:1091`
- **`new T{...}`** with a value inside — list-initialisation; the empty pair
  value-initialises. `src/parser/ParserExprNew.cpp:1117`

## Statements, exceptions and control

- **a local with a destructor and a `try` in one function** — refused now only
  where the call-site table cannot be split (`ParserStmt.cpp:797`), and on
  x86_64-windows only inside a `catch` handler, a cleanup there being a funclet
  inside a funclet. **The Itanium half went on 2026-09-09**, in three steps: a
  `try` in the same block as the local landed 2026-09-06; a local *inside* the
  `try`'s body landed 2026-09-07, its cleanup rows carrying the `try`'s catch
  types and handing the selector to one shared chain; and a local inside a
  **handler** landed with the end-catch region, which gave a handler's block
  the row it had been missing — the same region a `throw` out of a handler
  needs, and a local's destructor goes in it. **The Microsoft half went on
  2026-09-26**: beside a `try` and inside its body, through the FH3 state tree
  (CLAUDE.md, "The FH3 state tree on x86_64-windows").
  `src/parser/ParserStmt.cpp:797`, `src/parser/ParserStmt.cpp:1090`
- **a `goto` that leaves a `catch` handler** — [except.handle]/16 ends the
  handling on the way out and the call that ends it is `__cxa_end_catch`, which
  a jump has to make where it is written. A forward label has not been read
  there: `resolveGotos` fills the jump's cleanups afterwards, and by then the
  statement is built. `return`, `break` and `continue` out of a handler all
  work, and so does a `goto` to a label the handler itself declares.
  `src/parser/ParserStmt.cpp:2042`
- **a class declared in the condition of a `while`** — [stmt.iter]/2 builds it
  afresh on every turn and destroys it at the end of each one, and the
  construction would have to be written where the test is. A scalar works, and
  so does a class in the condition of an `if`, where the object is built once.
  `src/parser/ParserStmt.cpp:738`
- **a `try` inside a `catch` handler, on x86_64-windows only** — a `try` inside
  a `try` body works there since 2026-09-26, and a handler's own `try` would
  put funclets inside a funclet. Both shapes work on both Itanium
  targets as of 2026-09-07, body and handler alike, which took three things: the
  enclosing region split around the inner one so no two rows overlap, the action
  chain continuing outwards so phase 1 finds an enclosing `catch` on the inner
  row's chain, and the inner pad handing the selector to the enclosing chain
  rather than calling `_Unwind_Resume`, which leaves for the caller. A Microsoft
  handler is a funclet named `<fn>$catch$N` from a per-function counter, so a
  nested one takes a name already used and ml64 answers
  `A2005: symbol redefinition`; all three mechanisms are to the Itanium
  call-site list; the Microsoft ABI has the FH3 state tree instead. See
  CLAUDE.md, "A `try` inside a `try`". `src/parser/ParserStmt.cpp:1132`
- **a rethrow**, `throw;` with nothing after it — **for x86_64-windows
  only**; it works on both Itanium targets. There it is
  `_CxxThrowException` with two null pointers, raised from inside a handler
  funclet rather than from the frame that owns the `try`, which has not been
  measured on the box. `src/parser/ParserStmt.cpp:1674`
- **a dynamic exception specification**, `throw(T)` — `throw()` with nothing in
  it is `noexcept` and works. `src/parser/ParserConst.cpp:87`
- **a range-based `for` over a temporary** — [stmt.ranged] binds the range to
  `auto &&`, whose lifetime extension keeps it alive for the whole loop; the
  range is held as a pointer here, so a temporary would be walked after it
  died. Name it in a variable first, which is what the message says.
  `src/parser/ParserStmt.cpp:467`
- **a range-based `for` over a class with no member `begin`/`end`** — the free
  `begin(r)`/`end(r)` found by argument-dependent lookup, which
  [stmt.ranged]/1 falls back to, is not looked for. The member form works, and
  is what every container in `include/` provides. `src/parser/ParserStmt.cpp:474`
- **a range-based `for` whose iterator is a class** — every `begin()` in
  `include/` returns a pointer and the loop built is the pointer loop; a class
  iterator wants its `operator!=`, `operator++` and `operator*` resolved
  instead. `src/parser/ParserStmt.cpp:509`
- **a reference loop variable in a range-based `for`** —
  `src/parser/ParserStmt.cpp:562`
- **a trailing return type**, `auto f(int) -> int` — C++11, and refused as the
  C++11 feature it is rather than as `auto` deduction.
  `src/parser/ParserTopLevel.cpp:578`
- **a function try block**, `int f() try { } catch (...) { }` — its handler
  covers the mem-initialisers as well as the body, so a `try` inside the body
  is not the same thing. C++98. `src/parser/ParserTopLevel.cpp:1165`
- **a range-based `for` over a braced list** — the list would be an
  `std::initializer_list`, which there is no library for.
  `src/parser/ParserStmt.cpp:553`

## Namespaces and lookup

- **a namespace alias**, `namespace A = N;` —
  `src/parser/ParserTopLevel.cpp:79`
- **a using-declaration inside a class**, `using B::f;` — it redeclares a base
  member rather than naming one, changing its access and joining the derived
  class's overload set. `src/parser/ParserType.cpp:553`
- **a using-declaration inside a block** — it would declare a name for the rest
  of the block and rank against the locals beside it.
  `src/parser/ParserStmt.cpp:1653`. The one at namespace scope,
  `using N::f;`, works, and so does `using namespace N;` here.
- **an alias declaration**, `using X = T;` — `typedef T X;` says the same
  thing here. It is not a using-declaration, and the three scopes that refuse
  that one must not answer for this. `src/parser/ParserConst.cpp:16`
- **an inline namespace** — its members would have to be found in the
  namespace around it. `src/parser/ParserType.cpp:1595`

## Lambdas

- **naming a capture after a default one** — `[=]` and `[&]` on their own take
  everything the body reads, `this` included where the body names a member.
  `src/parser/ParserExprLambda.cpp:198`
- **a capture-less lambda converting to a function pointer** —
  [expr.prim.lambda]/6 gives the closure a conversion function returning one
  that calls the body, and that function is not synthesised.
  `src/parser/ParserOverload.cpp:1060`

## Lexer and preprocessor

- **GNU's named variadic macro parameter** — write `...` and use `__VA_ARGS__`.
  `src/Preprocessor.cpp:926`
- **a raw string literal**, `R"(...)"`, `u8R`, `LR`, `uR` and `UR` — an
  ordinary `"..."` is a narrow string of char here. `L`, `u`, `U` and `u8`
  string literals and `u`/`U` character literals are read and work.
  `src/Lexer.cpp:392`
- **a `u8` character literal** is C++17. `src/Lexer.cpp:389`

## Refused because of the standard version

C++11 is the target, so a C++14 or C++17 form is refused *naming the version*
rather than as a missing feature. The standing rule is that when a C++11
feature in this table's neighbourhood is built, the version-boundary refusal
beside it goes in the same commit.

| written | version | site |
| --- | --- | --- |
| `1'000`, a digit separator | C++14 | `src/Lexer.cpp:189` |
| `0b101`, a binary literal | C++14 | `src/Lexer.cpp:318` |
| `decltype(auto)` | C++14 | `src/parser/ParserExpr.cpp:1430` |
| `[n = k]`, an init-capture | C++14 | `src/parser/ParserExprLambda.cpp:231` |
| `auto` as a parameter type | C++14 | `src/parser/ParserClass.cpp:4005`, `src/parser/ParserTopLevel.cpp:652` |
| `auto` as a return type | C++14 | `src/parser/ParserTopLevel.cpp:583` |
| a variable template | C++14 | `src/parser/ParserTemplate.cpp:380` |
| `S s = {1, 2}` with an NSDMI — not an aggregate in C++11 | C++14 changed the rule | `src/parser/ParserInit.cpp:682`, `src/parser/ParserInit.cpp:891`, `src/parser/ParserTopLevel.cpp:359` |
| `static_assert` with no message | C++17 | `src/parser/ParserConst.cpp:47` |
| `namespace N::M { }` | C++17 | `src/parser/ParserTopLevel.cpp:76` |
| an attribute, `[[noreturn]]` | none parse | `src/parser/ParserType.cpp:1809` |

## `volatile`, which is read and dropped

**The fourth bucket, and the one a sweep of refusals cannot see**: cxx1 accepts
`volatile` and throws it away, so it answers and the answer is wrong. On an
*object* that costs nothing - cxx1 optimises nothing, so every read is already a
load - and those shapes are still accepted. Where the qualifier reaches a type a
linkage name is made from, it is refused. Measured 2026-09-07 over 24 shapes;
see CLAUDE.md, "The volatile sweep".

| refused | measured against | site |
| --- | --- | --- |
| a pointer or reference to a volatile type | clang `_Z1fPVi`, cl `PECH` | `src/parser/ParserType.cpp:1124` |
| `T *volatile` | cl `REAH`; Itanium right by accident | `src/parser/ParserType.cpp:1467` |
| a volatile member function | the cv on `this` is in the name on both ABIs | `src/parser/ParserType.cpp:966` |
| a typedef of a volatile type | it would launder the qualifier past the above | `src/parser/ParserType.cpp:1501` |

Accepted and unchanged: a volatile local, a volatile local array, a volatile
by-value parameter, an internal-linkage volatile object, a volatile data member.
`tests/cases/volatile-object.cpp` is that half.

## Keywords the parser has no rule for

Eight, from `pending[]` in `src/parser/Parser.cpp`. Each is refused **by
name** at the three doors a keyword can arrive at — an expression, a member
declaration, and a name — rather than as a parse error further along:
`src/parser/Parser.cpp:96`, `src/parser/ParserExpr.cpp:659`,
`src/parser/ParserType.cpp:1813`.

    alignas   alignof   asm       export    thread_local   typeid

`char16_t` and `char32_t` left it on 2026-09-26 by being implemented.

**Eleven left this list on 2026-09-06 by being implemented**: `and`, `and_eq`,
`bitand`, `bitor`, `compl`, `not`, `not_eq`, `or`, `or_eq`, `xor` and `xor_eq`
are alternative spellings of operators, and the lexer now writes each as the
operator it spells, so none of them reaches a parser rule as a word.

**A second list beside it says the opposite thing**, and the distinction is the
point: `implementedElsewhere[]` holds `catch`, `friend`, `inline`, `mutable`,
`namespace`, `operator`, `template`, `using` and `virtual` — all implemented,
none of which begins an expression, so the answer is *"it is implemented, but
it does not begin an expression"* and not *"not supported yet"*. Those eight
sat in `pending[]` until 2026-09-02 and the compiler was lying about itself in
eight places. **Measure with a one-line program in a place the keyword belongs
before moving a name between the two lists**; the review that first named them
guessed wrong twice.

## Refused for one target only

- **a virtual function overridden from a base that is not the first, on the
  Microsoft ABI** — cl compiles such an override against a biased `this` where
  Itanium puts a thunk in front, so this is a difference in code generation
  rather than in naming. `src/parser/ParserClass.cpp:1762`

- **a `volatile` object with external linkage, on x86_64-windows** — cl
  decorates a variable's name with its cv, `?g@@3HC` where a plain int is
  `?g@@3HA`, and neither Itanium target decorates a variable at all. So it is
  right on two targets and would be silently wrong on the third.
  `src/parser/ParserType.cpp:1479`

A case that cannot be compiled for a target names it in `<case>.notarget` with
the reason on the line, which is printed on every run.

## Three the tool finds that are not exclusions

`tools/exclusions` cannot tell a refusal from an ordinary diagnostic that
happens to say "yet", and filtering them inside the script would hide a
judgement in a program. They are named here instead:

- `src/parser/ParserType.cpp:215` — a base class that is not yet defined. An
  ordinary error: a derived object contains its base, so the base has to be
  complete.
- `src/Mangle.cpp:597`, `src/Mangle.cpp:1131` — a type with no Itanium or
  Microsoft linkage name. Internal: reaching either means a type was built that
  the mangler was never taught, which is a bug in this compiler and not a
  statement about the language.

## The rule this file leaves behind

**A refusal and a claim are the same kind of thing, and both need an oracle.**
A refusal that names a feature the compiler now has is worse than none, because
it is a claim the compiler cannot support — that is why `pending[]` was
measured keyword by keyword. A *headline* the compiler cannot support is the
same defect one level up, which is what this document fixes. When a feature
lands, delete its entry here and run `tools/exclusions --check`; when a refusal
is added, cite it here in the same commit.

## Every refusal site the sections above do not cite

Written by `tests/out-measure/regen-excl.py` from `tools/exclusions` on
2026-09-26, one line per site, the message as the compiler prints it with a
runtime piece shown as `<...>`. A site here has no prose above it yet; the
list exists so that `tools/exclusions --check` reads 0 uncited and 0 stale,
which is the only oracle this document has.

### `src/parser/Parser.cpp`

- 'alignas(<...>)' on a local is not supported yet: the stack is kept <...>-aligned on <...>, and a local past that would need the frame realigned. A global or a member may ask for more - `src/parser/Parser.cpp:744`

### `src/parser/ParserClass.cpp`

- '<...>::<...>' returns '<...>' over the base's '<...>' - a covariant return, and this one moves the result by <...> bytes at every call through the base, which needs a thunk this compiler does not build yet. Return the base's type here - `src/parser/ParserClass.cpp:101`

### `src/parser/ParserExpr.cpp`

- '<...>' is virtual but has no slot in '<...>'s own vtable - a function of a base after the first is not supported here yet - `src/parser/ParserExpr.cpp:2162`

### `src/parser/ParserInit.cpp`

- an initialiser for an array of '<...>' is not supported yet - each element gets the default constructor - `src/parser/ParserInit.cpp:1116`

### `src/parser/ParserStmt.cpp`

- a 'try' inside a 'catch' handler is not supported yet for x86_64-windows - a handler there is a funclet, and a nested try's handlers would be funclets inside it - `src/parser/ParserStmt.cpp:1132`
- catching a pointer by reference - '<...>' - is not supported yet: the runtime hands a handler the pointer itself, so catch it by value - `src/parser/ParserStmt.cpp:1247`
- a local with a destructor and a 'try' in one function is not supported yet - each is a range in the call-site table and one would have to split the other - `src/parser/ParserStmt.cpp:1955`

### `src/parser/ParserTemplate.cpp`

- '<...>' is a function template, and naming one without calling it is not supported yet - `src/parser/ParserTemplate.cpp:1797`
- '<...><...>::' - naming a member through a class template's argument list is not supported yet; the type itself is made, so 'typedef <...><...> Name;' and then 'Name::' reaches the member - `src/parser/ParserTemplate.cpp:1824`
- '<...>' is a classfunction<...> template, and instantiating one is not supported yet - `src/parser/ParserTemplate.cpp:1828`

### `src/parser/ParserTopLevel.cpp`

- '<...>(...)' initialises a virtual base with an argument that needs a temporary of its own, and that is not supported yet - a virtual base is built by the most-derived constructor, in a body whose frame this temporary is not in. An argument made of the parameters and constants works - `src/parser/ParserTopLevel.cpp:967`

### `src/parser/ParserType.cpp`

- '<...>::<...>' names a static member through the template's argument list, which is not supported yet - a typedef for '<...>' reaches it: typedef <...> B; B::<...> - `src/parser/ParserType.cpp:41`
- this <...> is under '#pragma pack(<...>)', and the C6000 backend loads words aligned only - a packed member is not supported for this target yet - `src/parser/ParserType.cpp:60`
- '<...>' has virtual functions in a base that is not the first, and the Microsoft ABI lays that out differently - the base with the vfptr goes first, wherever it was written. Not supported yet; it is measured for Itanium only - `src/parser/ParserType.cpp:298`
- '<...>' has a virtual base '<...>' with virtual functions, and the Microsoft ABI dispatches through that base with vtordisp fields and thunks of its own - not supported for this target yet; the Itanium targets and the C6000 compile it - `src/parser/ParserType.cpp:440`
- '<...>' is not supported yet: an override is found by its base's slot here whether or not the word is written, so this would be a check rather than a change - `src/parser/ParserType.cpp:986`
- a braced member initialiser with values is not supported yet - '= {}' and '= {0}' zero an array or a scalar member; write the rest as a value - `src/parser/ParserType.cpp:1114`
- a pointer or reference to a 'volatile' type is not supported yet - the qualifier is not in this compiler's type system, so this would be named 'int *' where clang writes 'PVi' and cl writes 'PECH'. A 'volatile' object of its own is read and written here as it should be, and is not refused - `src/parser/ParserType.cpp:1494`
- the attribute '[[<...>]]' is not supported - C++11 has '[[noreturn]]' and '[[carries_dependency]]', which are read; '[[deprecated]]' is C++14 - `src/parser/ParserType.cpp:1561`
- '<...>::*' before '<...>' is complete - the Microsoft ABI sizes a pointer to a member by the class's inheritance model, and an incomplete class takes the widest form (cl's unspecified model, 12 and 24 bytes). Not supported yet; define the class first - `src/parser/ParserType.cpp:2249`
