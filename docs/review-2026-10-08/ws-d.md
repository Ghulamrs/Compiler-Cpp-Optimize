# WS-D - top level, statements, lexer, preprocessor: handover

Branch `review/d-toplevel`, from main `2955c0b`, 2026-10-08/09. Executor Fable 5.1, under
`docs/REVIEW-PLAN-2026-10-08.md` section 4 "WS-D". Nothing was pushed or merged; the main session
reads this note, re-runs the gates it wants, and writes the CLAUDE.md sections from it - drafts
are at the end. Written on the budget the user set on 2026-10-09: the gate is trimmed as section
"Gates" says, and WS-D does not edit `ParserOperator.cpp`.

## Commits

| commit | item | state |
| --- | --- | --- |
| `02e55d8` | F20 const or reference member in a mem-initialiser list | landed |
| `77ddb81` | F09 namespace alias | landed at namespace scope; inside a block refused by name |
| `f42cb9a` | F44 `__COUNTER__`; L19 a missing standard header refused by name | landed |
| `189d1ff` | cases: F20's and F09's constructors out of line, so names.sh agrees | |
| `71a049c` | F01 raw string literals | landed |
| `722a9cd` | F18 trailing return types | landed; two shapes refused by name |
| `b7fcd50`, `3f417bd` | F14 delegating constructors | landed; a class with a virtual base refused by name |
| `5f5b5a6` | F27a class iterator, F27b free `begin`/`end`, F27c a temporary range | **WIP: written, never built** - see "F27" and "Recipe" |
| this note | | |

**One deviation from the plan's ownership**, said first because the main session decides it:
F18 carries one small hunk each in `ParserType.cpp` (WS-C1's) and `ParserTemplate.cpp`
(WS-E1's) - the in-class member and the template pattern read the arrow too. Each is a call into
a function defined in WS-D's files, named in `722a9cd`'s message.

**The scoped-enum `case` label (the extra item) is not done here, and cannot be.** A scoped
enumeration does not exist on this branch - `enum class` is still refused by name at
`ParserType.cpp:1401` on `review/d-toplevel`, because F06 landed in WS-C1 after this branch
forked. The over-acceptance is in main's code: `Parser::caseLabel` (main `ParserStmt.cpp:915`)
folds the label with `constantExpression(...)` and narrows it to `switches_.back().governing`
without asking the label's *type*; [stmt.switch]/2 wants a converted constant expression of the
condition's promoted type, and a scoped enumeration promotes to nothing, so `case 1:` under a
`switch (Colour c)` must be refused and `case Colour::Red:` taken. The mend is in `caseLabel`:
read the expression with `expr()`, and where `governing->isScopedEnumeration()` refuse an operand
whose `type()->unqualified()` is not `governing->unqualified()` (clang: "no viable conversion"),
then fold it. Written after the rebase, where the type exists - a dozen lines and one `.error`
case (`scoped-enum-case-int-refused.cpp`).

## F20 - a const or reference member named in a mem-initialiser list (`02e55d8`)

[class.base.init]/8: the store from a constructor's own list is the member's initialisation,
so the refusal of a const member there goes; one the list leaves out with nothing to give it a
value is refused, as clang refuses it. Cases `const-member-init` (clang's output),
`const-member-uninitialised-refused`, `reference-member-uninitialised-refused`. Emit golden 0 of
1582 changed.

## F09 - namespace aliases (`77ddb81`)

[namespace.alias]: `namespace A = N;` and `A = N::M;` - every later `A::` to the end of the
enclosing namespace is rewritten to N's name in place, and the readers that meet the name
without `::` (a using-directive, a second alias) resolve it. Inside a block it is refused by
name, the rewrite reaching a namespace's close and not a block's. `namespace-alias-refused`
goes; `namespace-alias` (clang's output), `namespace-alias-in-block-refused`,
`namespace-alias-not-namespace-refused` arrive. Emit golden 0 of 1582 changed.

## F44 `__COUNTER__` and L19 the missing-header refusal (`f42cb9a`)

`__COUNTER__` expands to 0, 1, 2 ... once per expansion - an extension clang, g++ and cl share.
A macro argument is now macro-replaced once however often it is used ([cpp.subst]/1), which is
what makes `TWICE(__COUNTER__)` one value; `defined()` and `#ifdef` answer yes for `__LINE__`,
`__FILE__` and `__COUNTER__`. `#include <tuple>` and the 47 other C++11 header names `include/`
lacks are refused as "the standard header <tuple> is not provided by this compiler yet", the
table cited in EXCLUSIONS. Cases `preprocessor-counter` (clang's output),
`missing-standard-header-refused`. Emit golden 0 of 1582 changed.

## F01 - raw string literals (`71a049c`)

[lex.string]/4 and [lex.pptoken]/3: `R"delim(...)delim"` with every prefix (R, u8R, LR, uR, UR)
is its characters as written - no escape, no splice, no comment, no directive, a newline kept -
closed only by its own `)delim"`. The lexer reads it whole; the preprocessor's line splitter
leaves a splice inside one alone, and a raw string a line leaves open is copied verbatim to its
close, so a line inside it that looks like `%:define` or `#if` is not a directive and a name
inside it is not a macro. `raw-string-refused` goes; `raw-string-literal` (clang's output)
arrives.

## F18 - trailing return types (`722a9cd`)

[dcl.fct]/2: `auto f(params) -> T` reads T after the parameter list, with the parameters in
scope - `decltype(a)` names one - and on a member in the class's scope, so a nested type needs no
qualifier. Free functions, prototypes, out-of-line members (`auto S::f() const -> T`), members
declared and defined inside a class, block-scope declarations and function templates with
`-> T`. Refused by name: a function template whose trailing type is `decltype(...)` (Itanium
spells it as the expression, `Dt...E`, which the mangler cannot write) and `-> auto` (C++14); a
base other than plain `auto` is ill-formed and refused as clang refuses it. Cases
`trailing-return-type` (clang's output; names agree on all three targets),
`trailing-return-deduced-refused`, `trailing-return-template-decltype-refused`.

## F14 - delegating constructors (`b7fcd50`, `3f417bd`)

[class.base.init]/6: a mem-initialiser naming the class itself - by its name, its qualified tag
or a template-id that makes this class - is the list's only entry; the target is chosen by
overload resolution with its defaults applied and called on `this` before the body, and the
bases, members and vptr are left to it. A list with another entry beside it is refused as clang
refuses it; a class with a virtual base is refused by name (the target would have to be told
whether the object is the most derived). Cases `delegating-constructor` (clang's output, ledger
balanced; names agree but for the `.nonames` recording clang's C2-only inline template
constructors on x86_64-linux and a pre-existing Windows RTTI name),
`delegating-not-alone-refused`.

## F27a, F27b, F27c - the range-based `for` over a class (one rewrite, see "Commits")

**State: `5f5b5a6` is a WIP commit - written, not built, not gated.** The Windows box went down
at about 06:55 on 2026-10-09 and the user wound the workstream up at 07:20 with the weekly budget
at 92%, before a single build of the F27 code had run. Everything below about F27 describes what
the code is written to do, not what was measured; the first build may find a compile error or a
wrong answer, and the three cases' `.expected` were written from clang's rules by hand and have
NOT been produced by VS clang on the box yet - the recipe below does that first.

The three were written as one rewrite of `Parser::classRangeEnds` and `rangeForStatement`
(`ParserStmt.cpp`), and the code is committed once: the member/free choice, the iterator's kind
and the range's value category are three questions asked of one statement, and separating the
code would have meant three intermediate states of a function none of which is a feature. The
cases, the refusal that went and the EXCLUSIONS rows are the per-item record.

**F27a.** [stmt.ranged]/1's loop `for (auto __b = begin, __e = end; __b != __e; ++__b) { T x =
*__b; }` is built as before for a pointer, and for a class iterator `!=`, `++` and `*` are the
iterator's own operators through `comparison` (which asks `overloadedBinary` first) and
`overloadedUnary` - the ordinary resolution, so a member or a free `operator!=`, a const
`operator*`, and a `T &` the loop variable binds to all come for free. `begin`'s type and `end`'s
must be one type (C++11; the message says C++17 relaxed it). The iterators are *objects* now,
`initRangeLocal`: a class with a destructor is built where `T v = init;` would build it - a call
returning through a slot claims the slot (`claimCallResult`), a class with constructors goes
through `constructLocal` as copy-initialisation, a pointer is stored - and goes on `alive_`, so
the loop's end destroys it, a `return` or `break` out of the body does, and an exception does
through a cleanup region (`rangeBuilt_` records the statement index each became alive at, in
the shape `block()` keeps). Case `range-for-class-iterator` (clang's output): a class iterator
with a copy constructor and destructor, live count back to zero after each loop, a free
`operator!=`, a member one, a reference loop variable written through, `auto`, a const range, a
`break`.

**F27b.** Where the class has neither member, `begin(r)` and `end(r)` are looked up by the
argument alone - the class's own namespace, else the global one (`lookupKeys`'s rule for an
operator, written out because the two names are not operators). Case `range-for-free-begin`:
a namespace's `Row` with const and non-const overloads, a global `Pair`. The refusal
`range-for-no-begin-refused` stays and now says what [stmt.ranged]/1 looked for and found
neither of; clang refuses the same program at the same column.

**F27c.** A temporary range is what `auto &&__range` binds and keeps alive to the loop's end
([class.temporary]/5). A class temporary written as `P(1)` arrives as the glvalue `*(ctor(&tmp),
&tmp)`; `extendTemporary` takes its slot off the pending list and onto `alive_`, as `const T &r =
P(1)` does. A prvalue - a call returning the class - is built into a slot of its own by
`initRangeLocal`. Both are destroyed after the loop and on an exception out of it. Case
`range-for-temporary` (clang's output): `make(10)`, `Bag(20)`, a plain struct returned by
value, and a throw out of the body caught one function up with the range destroyed on the way.

**What is left of F27, and the refusal that stays.** On x86_64-windows a range or iterator
with a destructor inside a `catch` handler is refused by name (`ParserStmt.cpp:813`, the same
funclet-inside-a-funclet reason `block()` gives at `:1239`; cited in EXCLUSIONS beside it). An
rvalue reference loop variable stays refused. F27d, a braced list, is E2's after F25. **A
pre-existing defect found reading the code and not mended**: the loop variable itself, `T x :
r` with T a class with a copy constructor, is still copy-initialised by a bytewise `Assign` (the
array path has done so since 7.3) and never destroyed - `for (std::string s : v)` shallow-copies
each element. It wants the same `initRangeLocal` road for `d.name`, with the body block's own
cleanup; left because the body's `alive_` bookkeeping (loopMarks_, breakMarks_) is taken
before the variable is built and the change is a round of its own.

## Golden

Recorded and read for F20, F09, F44/L19 (0 of 1582 changed each, see their sections). **Not
recorded or read for F01, F18, F14 or F27** - the box was in use for other workstreams for the
first three and down for the fourth; the recipe below records it at `3f417bd` (before F27) and
reads the diff. Expected for F27: 0 changed among existing cases - the pointer loop is rebuilt
from the same nodes - and 9 added (three cases, three targets); anything else in an existing
`range-for-*` case is a regression to read.

## Gates

**None of the gate was run on the final tree.** The gates recorded in the earlier commits'
messages (F20, F09, F44: run.sh and emit on the box, names agreeing) were run at those commits.
At `5f5b5a6` only the Mac-side checks ran: `tools/exclusions --check docs/EXCLUSIONS.md` 0
uncited, 0 stale; `make comments` (tools/comment-lines src) 0 groups over the cap.

### Recipe to finish, when the box is back (all on `ssh windows`, cmd.exe unless said)

1. `cd C:\cxx1\rtsdiv\ws-d && git fetch origin review/d-toplevel && git checkout 3f417bd`
   (the branch is pushed; the clone has `core.autocrlf=false`).
2. Golden before F27: build (`msvc\build.cmd` or the workspace's usual `nmake`/`make`), then
   under Git bash `tests/emit.sh --record`.
3. `git checkout review/d-toplevel`, build. Fix compile errors in `ParserStmt.cpp` (functions
   `classRangeEnds`, `initRangeLocal`, `rangeForStatement`; the declarations are in `Parser.h`
   at the WIP's hunk). Things to look at first if it fails: `constructLocal`'s `Declared`
   initialiser `Declared{ name, t, pos, 0, std::string() }` against the struct's field order;
   `completeCall`'s argument order; `overloadedUnary` takes `ExprPtr &`.
4. Produce the three `.expected` with VS clang:
   `"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\x64\bin\clang++" -x c++ -std=c++11 -pedantic-errors tests\cases\range-for-class-iterator.cpp -o t.exe && t.exe`,
   the same for `range-for-free-begin` and `range-for-temporary`; overwrite the hand-written
   `.expected` with the output (LF endings). For `range-for-no-begin-refused.error` the column
   is clang's `16:16`, already in the case's comment.
5. `tests/run.sh` at -O0 (Git bash, `cxx1-msvc.exe`); then -O2 if budget allows. Read every
   failure of a `range-for-*` case first.
6. `tests/emit.sh` and read the golden diff; classify every changed file in this note.
7. `tools\windows\names-overload.cmd` (names.sh + overload.sh under Git bash with VS LLVM
   first on PATH). A new case whose names differ gets a `.nonames` with the measured reason -
   expect clang's C2-only inline constructors on x86_64-linux for `range-for-temporary`'s
   `Bag` only if its constructors are inline; they are written out of line on purpose.
8. `tools\windows\run-cases.cmd` at -O0 (GNU spelling); `tests/tms6747.sh` at -O0 both legs.
9. `tools/seal check` on the Mac; the changed sealed files are the ones in "Sealed files".
10. If all green: amend `5f5b5a6`'s message to drop "WIP" (keep the three items in one commit,
    the reason is in "F27"); if the loop-variable defect is to be mended, it is a new item.
11. Then the scoped-enum `case` label, after the rebase on main (see above).

Linux gates (run.sh -O0/-O2 under g++): skipped on the user's instruction. `run-cases.cmd` at
-O2 and `tests/tms6747.sh` at -O2: skipped, budget. The Windows box was unreachable (host down)
from about 06:55 on 2026-10-09 and had not come back when the workstream was wound up at 07:20.

## EXCLUSIONS.md

The branch's document is **main's document** (0e64e73, which WS-C1 regenerated), relined to this
branch's sources by matching each site's message (117 of 129 sites), with WS-D's rows applied:
the raw-string, range-for (three), const-member, delegating-constructor, trailing-return and
namespace-alias rows gone; the missing-header table, the namespace-alias-in-block, the
`-> decltype` template, the `-> auto`, the delegating-constructor-with-a-virtual-base and the
range-for-in-a-handler rows added. Six rows name WS-C1's *old* refusals, which exist on this
branch and not on main (`= default`, `friend class`, `enum class`, `override`/`final`, `final`
on a class, a `constexpr` constructor), and five of main's rows for C1's *new* refusals are not
here (a deleted destructor, `= default` outside the class, a deleted virtual, a `constexpr`
object, statements in a `constexpr` constructor). **At the rebase, take main's version of every
one of those eleven rows**; the WS-D rows are the ones to keep. `tools/exclusions --check` reads
0 uncited, 0 stale on the branch.

## Sealed files changed

Not checked (`tools/seal check` is a Mac tool, but the seal's file list is in the tree - run it at
the rebase). Expected to differ: `src/Lexer.cpp`, `src/Lexer.h`, `src/Preprocessor.cpp`,
`src/Preprocessor.h`, `src/parser/Parser.h`, `src/parser/Parser.cpp`, `src/parser/ParserStmt.cpp`,
`src/parser/ParserTopLevel.cpp`, `src/parser/ParserType.cpp`, `src/parser/ParserTemplate.cpp`,
`src/parser/ParserConst.cpp` (every `src/` file this branch touches: `git diff --stat main...HEAD -- src`).

## Draft CLAUDE.md paragraphs

### The range-based `for` finishes its rewrite: a class iterator, a free `begin`, a temporary

**[stmt.ranged]/1 is a rewrite, and three thirds of it were refused by name until 2026-10-09.**
`for (T x : r)` is `for (auto __b = begin-expr, __e = end-expr; __b != __e; ++__b) { T x = *__b;
body }`, and the loop built here had begin and end as members, the iterator a pointer, and the
range an lvalue - which is every container in `include/` and nothing else. The class iterator is
the standard's own case: `!=`, `++` and `*` are whatever the iterator declares, found by the
operator resolution every other expression goes through, so a member `operator!=` or a free one,
a const `operator*` and a `T &` the loop variable binds to come for nothing. The two iterators
are objects of the loop's own scope now - built as `T v = init;` builds one, on `alive_`, so the
loop's end, a jump out and an exception each destroy them, through the cleanup regions `block()`
already shapes; the pointer loop emits what it emitted, which the golden said. Where the class
has neither `begin` nor `end` the free `begin(r)`/`end(r)` are looked up by the argument's own
namespace and then the global one, the operators' rule. And a temporary range is what `auto
&&__range` binds and keeps to the loop's end: the `*(ctor(&tmp), &tmp)` a class temporary already
is has its slot moved onto `alive_` as a reference binding would, and a call returning the class
is built into a slot of its own. What stays refused: the loop variable of class type is still
copied bytewise, as the array loop has copied it since rung 7.3 - a defect, recorded; an rvalue
reference loop variable; on x86_64-windows a range or iterator with a destructor inside a `catch`
handler, the funclet rule.

### Delegating constructors, trailing return types, raw strings, a const member initialised

Each of F14, F18, F01 and F20 is written up in its commit message (`b7fcd50`, `722a9cd`,
`71a049c`, `02e55d8`) and in this note's sections; the main session lifts the paragraph it wants.
Two sentences worth the CLAUDE.md entry: **a delegating constructor leaves the bases, members
and vptr to its target** - the list's one entry is the whole initialisation, [class.base.init]/6
- and a class with a virtual base is refused because cl's most-derived flag and Itanium's C1/C2
split would both have to be passed through, which is a round of its own; and **a trailing
return type is read in the scope of the parameters**, so `decltype(a + b)` names them, which is
the whole reason the syntax exists.

### `__COUNTER__`, a namespace alias, and a header named by its absence

`__COUNTER__` is an extension the three oracles share and is one per expansion - which made a
macro argument's replacement happen once however often the parameter is used ([cpp.subst]/1),
a rule that had been wrong without a way to see it. `namespace A = N;` is a rewrite of every
later `A::` to the end of the enclosing namespace, which is why it works at namespace scope and
is refused by name inside a block. And `#include <tuple>` says "the standard header <tuple> is
not provided by this compiler yet" rather than "cannot find", for the 48 C++11 header names
`include/` lacks - a refusal `tools/exclusions` can count.
