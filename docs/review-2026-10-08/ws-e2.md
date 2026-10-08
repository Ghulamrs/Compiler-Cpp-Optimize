# WS-E2 - initialisation, expressions, casts: handover

Branch `review/e2-init-expr`, from `2955c0b`. Commits: `f079b5f` F29 (by the
previous agent, Fable 5.1), `f85588a` F24/F25/F03, `7eb2696` F22, `52af7ad`
F41a/F41b, and the commit carrying this note (names markers, EXCLUSIONS lines).
Nothing pushed, nothing merged.

## Items

| Item | State | Cases |
| --- | --- | --- |
| F29 static_cast downcast | landed (`f079b5f`) | static-cast-downcast, static-cast-virtual-base-refused |
| F24 list-initialisation | landed | list-init-constructor, list-init-expression, list-init-qualified, new-list-init, braced-argument, list-init-narrowing-refused |
| F25 initializer_list argument | landed | initializer-list-argument |
| F03 auto from braces | landed for `auto`/`const auto x = {...}` | auto-braced-list, auto-braced-direct-refused, auto-braced-mixed-refused, auto-braced-no-header-refused, initializer-list-static-refused |
| F22 lambda to function pointer | landed; one refusal line for the main session | lambda-to-function-pointer, lambda-capturing-no-pointer-refused, static-member-function-address |
| F41a dynamic_cast to a reference | landed on the Itanium targets; x86_64-windows and `&&` refused by name | dynamic-cast-reference, dynamic-cast-rvalue-reference-refused |
| F41b dynamic_cast, several bases | landed on Itanium (it worked already - measured and cased); Microsoft refused by name | dynamic-cast-multiple-bases |
| F27d range-for over a braced list | **not done**: WS-D's F27a is not merged (main at `cc210bb` has WS-A only). It needs nothing new once D lands: a braced list is now an `initializer_list` whose begin/end are pointers, the iterator shape the class range-for already takes; the refusal is `ParserStmt.cpp:614`, WS-D's file | - |

Removed refusals and their cases: list-init-ctor, list-init-expression-refused,
list-init-values-refused, lambda-to-function-pointer-refused,
dynamic-cast-reference-refused.

## The one line for the main session (F22)

`src/parser/ParserOverload.cpp:1146` (WS-C1's file) refuses every closure
converting to a function pointer. A captureless closure now has a conversion
function, so `conversionFunction` answers before that line and it is reached
only by a lambda that captures. At merge, after C1: keep the test and change the
message's tail - drop "and that conversion is not supported yet" - so it reads
"a lambda converts to a function pointer only if it captures nothing - call the
closure directly, or give the function a template parameter for it". That is
[expr.prim.lambda]/6's rule, not a gap. `lambda-capturing-no-pointer-refused.error`
holds only the leading clause, so it passes before and after. (clang's column
is 4:11, ours 4:21.)

## What was built, item by item

**F24/F25** (ParserInit, ParserExpr, ParserExprCall, ParserExprNew, Parser.h).
[over.match.list] for a class with constructors: an initializer_list constructor
first, then every constructor with the braces' elements as arguments
(`listConstructor`), `explicit` refused for copy-list-init, [dcl.init.list]/7
narrowing checked per parameter. `T{...}` as an expression under every spelling
- plain, `N::S`, a typedef, a nested class, a class template-id plain and in a
namespace (`templateCall` in ParserTemplate.cpp and the namespace branch of
`primary` learned `{` beside `(`) - is `listTemporaryFrom`: a constructor's
temporary, an aggregate built in a hidden local (`aggregateTemporary`, guarded
for a destructor), a scalar from one element. `new T{...}` the same. A braced
call argument is held behind a `void` placeholder (`bracedPlaceholder`) until
the overload set says what its parameter is (`resolveOverloadBraced`): when
every candidate agrees on the type at that position the list is built against
it, else refused by name asking for `T{...}`. A `std::initializer_list<E>`
parameter gets the backing array in the caller's frame. And
`std::initializer_list<E> x = {...}` itself builds the list
(`isInitializerListType`), which F03 needed.

**F03** (ParserTemplate `deduceAuto`, ParserExprNew `deduceBracedAuto`,
`initializerListOf`). Each element's decayed type must be one E; the
specialization is instantiated through appended tokens naming a hidden typedef
`$ilelemN` (the lambda's `$lret` trick), so any E is one identifier. `auto
x{...}` is refused naming N3922; `auto &`/`auto *` from braces and a
static-storage list from braces are refused by name; no `<initializer_list>`
says to include it, at clang's column.

**F22** (ParserExprLambda `captureFreeConversion`). A captureless closure gets a
static `__invoke` - `((closure *)0)->operator()(args)`, a by-value class and an
rvalue reference handed on as xvalues - and a const conversion function
returning `&closure::__invoke`, both as synthesised tokens replayed with the call
operator. Itanium names agree with clang: `_ZZ4mainENK3$_0cvPFiiEEv`,
`_ZZ4mainEN3$_08__invokeEi`. Unused ones are pruned as any inline member is.
**Two defects found beside it and fixed**: `&S::f` for a *static* member function
was a pointer to member ([expr.unary.op]/3 - 2955c0b refuses
`int (*p)(int) = &S::twice;`); and a deduced lambda return type kept a scalar's
cv, `const int` for a member read through a const `this`, which the conversion
function's name spelled (`PFKi...`) - [expr.prim.lambda]/4 deduces after the
lvalue-to-rvalue conversion.

**F41a** (ParserExprNew `dynamicCastReference`, include/typeinfo). The pointer
cast on the operand's address; a null answer calls `__cxa_bad_cast`, which
throws std::bad_cast - what clang calls. `<typeinfo>` declares `std::bad_cast`
over the runtime's own (destructor and `what()` declared, defined there), as
`<new>` declares bad_alloc. x86_64-windows refused by name (a class throw is
refused there); tms6747 carries a `.notarget` - neither VM6747's runtime nor
RTS6x has `__cxa_bad_cast`/`bad_cast`.

**F41b**. On Itanium the refusal was unreachable: `emitClassTypeInfo` has
written `__vmi_class_type_info` since 10-06. Measured downcast, cross cast, a
diamond through virtual bases and a null answer - all print clang's output, and
2955c0b already did. The refusal's message now names x86_64-windows alone,
where `microsoftClassRttiNames` is gated to one base and the hierarchy record
(Masm.cpp and the GNU spelling) is written one base deep with mdisp 0 and no MI
flag. Lifting it there means per-base descriptors with displacements and the
multiple-inheritance attribute in both spellings, reachable only with a second
*non*-polymorphic base (a polymorphic one after the first is refused by
layout) - left as a named refusal.

## Gates (all on the final tree except where said)

- Linux box (g++ build), run.sh -O0: **640 passed, 0 failed**; run.sh with
  `CXX1="./cpp11.exe -O2"`: 639/1 - the one is "a quoted pattern with -S", the
  harness quoting `"$CXX1"` with a space in it, true of any tree run that way.
- Windows box, run-cases.cmd (GNU spelling), compared with .expected/.error:
  **-O0 616/0, -O2 616/0**. MASM spelling not run (after WS-F).
- names.sh on the Windows box: 405 passed, 11 failed. Six are 2955c0b's own
  (WS-A's four real - compare-with-zero, pipelined-data-exit,
  pipelined-reduction, ternary-index-after-remake - and the box's ifdef-comment,
  using-declaration-chain). The other five are this box's clang having no headers
  for x86_64-linux/arm64-darwin (auto-braced-list, initializer-list-argument,
  list-init-constructor, dynamic-cast-reference) plus dynamic-cast-multiple-bases,
  whose `.nonames` (C1/D1 of implicit specials) arrived after that run. The four
  were measured by hand on the box with `clang -target x86_64-linux-gnu -nostdinc
  -isystem include -isystem lib`: every defined name and every runtime call
  agrees except the C1/D1 inline forms, now in their `.nonames`. **arm64-darwin
  for those four wants a names.sh run on the Mac.** overload.sh **31/0**.
- emit golden (recorded from 2955c0b's compiler on the Linux box, 1582 files):
  **6 of 1582 changed, 45 added**. All six (lambda-capture-this-const and
  lookup-order on three targets) are the deduced-return fix: a lambda returning a
  member through a const `this` deduced `const int` and now `int`, and one
  redundant sign extension moves from the caller into the call operator
  (`movslq %eax, %rax` / `sxtw x0, w0`). Read every one.
- tms6747.sh on the Linux box at -O0 and -O2, against 2955c0b with the same
  tools (RIDE-5.0's vm6747/asm6x/lnk6x, sim6747 from ~/ride-5.1/SIM6747, RTS6x
  1.0 from ~/rts6x/build): the vm6747 leg has **no failure on either tree**; the
  sim6747 leg fails 195 (-O0) / 171 (-O2) cases on 2955c0b, every one "asm6x or
  lnk6x refused it" - the stale tool set, not the compiler - and this branch adds
  exactly its six new class/lifetime cases to that same failure, nothing else.
  The sim leg wants a current ASM6x/LNK6x/RTS6x 1.2 built on the box.
- `make comments`: 0. `tools/exclusions --check`: 132 sites, 112 uncited, 104
  stale - 2955c0b's document is WS-A's to regenerate (now on main); this
  branch's own refusals are cited with their lines in the sections it touched,
  to be re-cited when rebased on A.
- `tools/seal check`: not run (src changed, as expected; the reseal is step 12).

## Found and not fixed

- A class temporary returned by value (`return P(k, 2*k);`, `P` with a written
  copy constructor and a destructor) is built in the callee and its bytes moved
  to the caller's object, so the ledger reports `live=1 phantom=1`: the object
  destroyed is not the one constructed. Same at 2955c0b for `P(...)`; the new
  `P{...}` form inherits it. Cases keep returned temporaries off the ledger.
- A nested braced list in constructor braces, `P p{1, {2, 3}}`, and a braced
  argument through a function or member pointer are refused by name
  (`ParserExprNew.cpp:288`, `ParserExprCall.cpp:488`).
- `new T[n]{...}` with values stays refused (`ParserExprNew.cpp:1388`).

## Draft CLAUDE.md sections

### List-initialisation, every form, and a braced argument, 2026-10-08

**`P p{1, 2}`, `P p = {1, 2}`, `P{1, 2}`, `new P{1, 2}` and `f({1, 2})` were all
refused**, by three messages, while the empty pair and an initializer_list
constructor worked. [over.match.list] is two phases on top of `resolveOverload`:
an initializer_list constructor takes the braces whole, and otherwise every
constructor is a candidate with the elements as its arguments - `explicit`
refused for copy-list-initialisation, every element checked for narrowing
against the parameter it reaches. `T{...}` is the temporary `T(...)` makes, under
every spelling of T, an aggregate built in a hidden local and a scalar from its
one element. **A braced argument waits for its parameter**: it is held behind a
placeholder until the overload set is known, built against the type every
candidate agrees on at that position, and refused by name where they disagree.
An `initializer_list<E>` parameter gets its backing array in the caller's frame.
Measured against clang under the ledger; the golden did not move.

### `auto x = {1, 2}`, 2026-10-08

[dcl.spec.auto]/6 deduces `std::initializer_list<E>`, every element deducing one
E. The specialization is instantiated through appended tokens naming a hidden
typedef, so any E is one identifier; the variable then takes the braces the way
`std::initializer_list<int> x = {...}` does, which was itself refused until this.
`auto x{1}` stays refused naming N3922, which compilers apply to C++11.

### A lambda that captures nothing converts to a function pointer, 2026-10-08

[expr.prim.lambda]/6: the closure gets a static `__invoke` calling `operator()`
on a null closure, and a conversion function returning its address - clang's
shape and clang's names, `cvPFiiE` and `8__invoke`. Both are synthesised tokens
replayed with the call operator, so overload resolution finds the conversion by
the ordinary road. It found two older faults: `&S::f` of a static member function
was a pointer to member, and a deduced lambda return kept a scalar's `const`.

### `dynamic_cast` to a reference, and through several bases, 2026-10-08

A reference cast is the pointer cast on the operand's address, a null answer
calling `__cxa_bad_cast` - the Itanium runtime's thrower of std::bad_cast, which
`<typeinfo>` now declares over the runtime's own. Through two bases, a diamond
and a cross cast the runtime walks the `__vmi_class_type_info` written since
10-06; the refusal had been stale on Itanium and is Microsoft's alone now, whose
RTTI hierarchy is one base deep. x86_64-windows refuses the reference form until
a class can be thrown there.
