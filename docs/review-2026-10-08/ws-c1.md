# WS-C1 - class declarations, first half: handover

Branch `review/c1-class`, from main `2955c0b`, 2026-10-08. Executor Fable 5.1 (F12 and most of
F13), finished by Claude Opus 5.5 (the rest of F13, F04, F06, F19, the gates and this note), under
`docs/REVIEW-PLAN-2026-10-08.md` section 4 "WS-C1". Nothing was pushed or merged; the main session
reads this note, re-runs the gates it wants, and writes the CLAUDE.md sections from it - drafts
are at the end.

## Commits

| commit | item | state |
| --- | --- | --- |
| `3c73fde` | F12 `override` / `final` | landed |
| `e4bc461` | F13 `= default` / `= delete` (and a g++ build fix for F12) | landed, three forms refused by name |
| `16565f5` | F04 `constexpr` constructor | landed as the runtime half; a constexpr object of class type refused by name |
| `9f51504` | F06 `enum class` | landed; one over-acceptance left in WS-D's file |
| `85609cf` | F19 `friend class X;` | landed |
| `e91dfcf` | names fixes for three new cases | |
| this note | | |

**One deviation from the plan's ownership, said first because the main session decides it.** The
plan gives `ParserExpr.cpp`, `ParserOperator.cpp` and `ParserExprCall.cpp` to WS-E2, and F06 and
F19 cannot work without a few lines there: an enumerator named `E::a` is looked up in `primary()`,
the built-in operators that must refuse a scoped enumeration are written in `arithmetic()`,
`shiftOf()`, the unary operators and postfix `++`, and every access check funnels through
`isFriendOf`. There is no seam in WS-C1's files that sees those places. So the two commits carry
**seven inserted calls and two changed arguments** in E2's files, every one a call into a function
defined in C1's files, listed in each commit message:

- `ParserExpr.cpp`: `if (ExprPtr e = enumeratorThroughEnum()) return e;` in `primary()`;
  `refuseScopedOperand(...)` in `arithmetic()` and for unary `~` and `-`; `convert(..., true)` on
  `functionalCast`'s two conversions ([expr.type.conv] is a cast, and `int(e)` was refused).
- `ParserOperator.cpp`: `refuseScopedOperand(...)` in `shiftOf()` and for postfix `++`/`--`.
- `ParserExprCall.cpp`: `if (isFriendClassOf(cls)) return true;` at the top of `isFriendOf`.

Each is a one-line insertion, so a merge with E2's branch conflicts only where E2 rewrote the
same line; none of E2's commits at `f85588a` touches these lines.

## F12 - `override` and `final` (`3c73fde`, Fable 5.1)

`override` is a check after the slot search ("marked override but overrides nothing", clang's
sense); `final` on a member is recorded on its `VSlot` and refuses the next override; on a class
head it is recorded on the `Type` and refused in a base clause. Both contextual - `int final = 3;`
is a name. **The commit did not build with g++**: `struct VirtSpecifiers` used member initialisers
and was the default argument of a member function of the enclosing class, which g++ 11 refuses
("default member initializer ... required before the end of its enclosing class") and MSVC
accepts. Mended in `e4bc461` with a constructor. `override-final` now carries a `.nonames`:
x86_64-linux for clang's C2-only inline members, arm64-darwin for the C1 cpp11 emits for the two
abstract classes, which clang never emits; x86_64-windows agrees on all 66 names.

## F13 - `= default` and `= delete` (`e4bc461`)

`readDefaultedOrDeleted` reads the two forms where `= 0` sits, behind the virt-specifiers, and
the declare* that follows takes them - the constructor door, the destructor door and the member
door, so one reading serves all three.

- **`= default`** on a special member inside its class is the member the compiler would write:
  nothing is declared at the `= default`, `defaulted_` records which member and the access it was
  written under, and `declareImplicitSpecials` writes it as it would have - trivial where it is
  trivial (no symbol), synthesised where it has work. A defaulted default constructor beside a
  written one is declared, since `C c;` must find one. Measured against clang: a defaulted copy
  runs the members' copy constructors, a defaulted virtual destructor takes its slot.
- **`= delete`** sets `Signature::deleted`. Overload resolution ranks it like any other and
  `refuseDeleted` refuses it where it wins (`resolveOverload`); `markUsed`, `markSymbolUsed` and
  `findFunction` refuse the uses that do not go through resolution - a by-value argument's copy,
  the address of a function (`&f` was a link error).
- **Implicitly deleted**: [class.copy]/7 and /18 - a move written `= default` deletes the copies;
  /11 - a base or member whose copy constructor or copy assignment is deleted deletes the class's.
  These are declared deleted (and so refused at the use, with "the compiler would write it, and
  [class.copy] deletes it"). A defaulted move assignment moves the members that have one
  (`moveAssignOf`); a deleted implicit copy does not take the implicit default constructor away
  ([class.ctor]/5), and a protected or private defaulted member is declared so its access is
  checked.
- **Refused by name**: `= default` on a later declaration (it is user-provided there, and the
  implicit member is what stands in here); a deleted destructor (every destroy site would have
  to refuse it); a deleted virtual function (its slot would name nothing). EXCLUSIONS cites all
  three. A non-special member defaulted is refused as clang refuses it.

Probed against clang, beyond the cases: a deleted copy used by a by-value argument (refused, at
the call's statement rather than clang's column), a deleted `operator=`, a deleted overload chosen
by a `double` constructor argument, a protected defaulted constructor (refused at `P p;`), a
defaulted constructor declared twice (refused, different words).

**Found and not mended - not F13's**: a *written* protected destructor does not refuse `P p;`
(clang: "variable of type 'P' has protected destructor"); destructor access is checked for
members and bases and never for a local.

## F04 - `constexpr` constructors (`16565f5`)

The runtime half, as the plan asks. `constexpr` before a constructor is stepped over (the held
body's replay starts after it, as `explicit`'s does) and the constructor is an ordinary one:
`constexpr-constructor` builds three classes through constexpr constructors, calls a constexpr
member function on them, and agrees with clang. A `constexpr` object of class type is refused by
name in `specifiers()` - an object being a name followed by `=`, `{`, `;`, `,`, `[` or a
parenthesised initialiser (`atParenInitialiser`), so a constexpr function returning the class is
left alone. The C++14 neighbour, a statement in a constexpr constructor's body, is refused naming
the version, at clang's own column (4:47).

## F06 - `enum class` (`9f51504`)

- **The type.** `Type::isScopedEnumeration()` and `enumDefined()` beside the enumeration's tag;
  the kind is the underlying type's (int unless an enum-base says otherwise), so size, signedness
  and every backend are unchanged, and **mangling is unchanged** - an enumeration already kept its
  name on both ABIs; `enum-class` agrees with clang's names on all three targets.
- **The scope.** A scoped enumerator is keyed `E::a` alone (`n::E::a`, `C::E::a` by prefix);
  inside its own braces it is also keyed plainly, with the underlying type, and that key is put
  back at the `}`. `enumeratorThroughEnum` reads the longest `A::B::` that names an enumeration
  and its enumerator - so `E::a`, `n::E::a`, `C::E::a`, `T::a` through a template parameter, and
  C++11's `E::a` for an *unscoped* E, which was "'E' was not declared" before.
- **Opaque declarations**: `enum class E;`, `enum class E : short;`, `enum E : int;`; a
  redeclaration must repeat the key and the base.
- **Conversions** ([dcl.enum]/10, [conv.prom]/3): `promote` leaves a scoped enumeration alone;
  `rankArgument` gives it no rank against any other type; `convert` and `checkAssignable` refuse
  it in either direction unless the conversion is a cast; `usualArithmetic` lets two of one
  scoped type compare and nothing else mix; `requireScalar` refuses it as a truth value except as
  a switch's governing value (`atSwitchCondition`, the token before the `(`).
- **Operators**: `refuseScopedOperand` for the arithmetic, bitwise and shift operators, unary
  `+ - ~`, and `++`/`--` - the E2-file hooks above.
- **Over-accepted, left**: `case 1:` under a switch on a scoped enumeration - the case label is
  folded to a value in `ParserStmt.cpp` (WS-D's) before its type is known; clang refuses it.
  Reaching it needs the label's expression type, one line in that file.

## F19 - `friend class X;` (`85609cf`)

`friend class X;`, `friend struct X;`, and `friend X;` for a class already declared record X's
tag in `friendClasses_[owner]`; an undeclared X is named in the namespace around the class
([namespace.memdef]/3). `isFriendClassOf` asks whether the code being read is inside a member of
X, of a class nested in X, or a lambda written there; `isFriendOf` asks it first, so all of the
access checks honour it. Friendship is not inherited - `friend-class-not-friend-refused` is a
class derived from the friend, refused as clang refuses it.

## Gates

All on the Windows box, `C:\cxx1\rtsdiv\ws-c1`, a clone at `2955c0b` with the branch's files laid
over it, compiler built by `msvc\build.cmd`; the tree measured is `85609cf` plus the comment rewrap
and the names fixes of `e91dfcf`, which the last two rows re-measured.

| gate | result |
| --- | --- |
| Windows cases, GNU spelling, -O0 (`run-cases.cmd`, compared by `.expected`/`.error`) | 621 passed, 0 failed, 23 skipped by `.notarget` |
| the same at -O2 | 621 passed, 0 failed, 23 skipped |
| `tests/names.sh` (`names-overload.cmd`, VS clang 19.1.5) | 400 passed, 9 failed - the 6 WS-A found at base (`compare-with-zero`, `pipelined-data-exit`, `pipelined-reduction`, `ternary-index-after-remake`, and the box's `ifdef-comment`, `using-declaration-chain`) and 3 of this branch's, mended in `e91dfcf` and re-measured: `constexpr-constructor` (clang folded `twice`), `enum-class` (clang folded `const int Up`), `friend-class` (C2-only inline constructors on x86_64-linux, now a `.nonames`) |
| `tests/overload.sh` | 33 agreed with clang, 0 differed (31 at base, plus `scoped-enumeration` and `scoped-enumeration-not-viable`) |
| `tests/tms6747.sh` -O0, both legs (vm6747; sim6747 of RIDE 5.1 + asm6x, lnk6x, rts6x.lib) | 393 passed, 0 failed, 7 skipped for a 64-bit long, 7 not for this target; every pass also on sim6747 |
| `tests/tms6747.sh` -O2, both legs | the same: 393 passed, 0 failed |
| `tools/comment-lines` (`make comments`) | 0 groups over the cap |
| `tools/exclusions --check` | 130 sites, 114 uncited, 109 stale (base `2955c0b`: 131, 116, 112) - run on the Linux box before the stop below, since on Windows the tool compares backslash paths with the document's slashes and reports everything; none of this branch's six citations is reported |
| `tools/seal check` (Mac) | 9 of 155 sealed files differ, all `src/` files this branch changed - expected until the reseal |
| every intermediate commit | `src/` of each commit's index built on the box before committing |

**Linux box gates skipped, on the main session's instruction to stop all work there.** The g++
build of the branch's source completed (`./build -j2`, `-Werror`, cpp11.exe built - which is how
F12's g++ break was found) before the instruction; **`tests/run.sh` at -O0 and at -O2 on the
Linux box were started and killed unfinished, and `tests/tms6747.sh` was never run there**. Its
`~/ride-5.1/ws-c1` clone is left as it was, nothing running in it.

**Not run**: the MASM spelling (after WS-F merges, per the plan), and `tests/emit.sh --o2`.

## The emit golden

Recorded on the box at `2955c0b` (15:33 UTC, clean tree) before the first change, compared
after the last with `tests/out-emit` regenerated by `tests/emit.sh` (1590 passed, 0 failed):

    golden: 0 of 1582 files changed, 20 added, 0 removed

The 20 added are this branch's five run cases on four targets (`constexpr-constructor`,
`defaulted-members`, `enum-class`, `friend-class`, `override-final`); refusal cases emit nothing.
No existing case's emission moved for any target - the features touch only programs that were
refused before, and the implicit-special-member changes (a deleted implicit copy declared, the
default constructor rule of [class.ctor]/5) change nothing for a class that writes neither
`= default` nor `= delete`. (One run in between read 305 changed: a second `emit.sh` started
by a script race while the first was writing; the clean run reads 0, and the `.s` files of the
cases it named are byte-identical to the golden.)

## What is left, by item

- F13: `= default` outside the class; a deleted destructor; a deleted virtual function (named
  refusals, cited). A written protected destructor's access for a local (not F13's).
- F04: a constexpr object of class type (named refusal) - the evaluator has no object model.
- F06: `case 1:` under a scoped switch (WS-D's `ParserStmt.cpp`).
- F19: befriending one member function of another class stays refused, as before.
- `tools/exclusions --check` cannot read 0/0 on this branch: the document's citations went stale
  before the branch (131 sites, 126 stale at `2955c0b`); WS-A regenerates it. Every citation this
  branch adds or changes is at its live line, checked by hand.

## Draft CLAUDE.md sections

Written for the main session to adapt; each follows the file's habit of saying what was
measured, what the oracle said and what is refused.

### `override` and `final`, checks and not declarations

**[class.virtual]/4 and /5 make both words checks on a slot the search already finds**, so
neither declares anything: `override` refuses a member whose slot search found no base virtual,
`final` on a member is recorded on its `VSlot` and refuses the next override, and `final` on a
class head is recorded on the `Type` and refused where a base clause names it. Both are
contextual - `int final = 3;` and `int override = 2;` stay names - and are read behind the
exception specification, where every declarator tail passes, so the destructor and `= 0` take
them too. **The first commit did not build with g++**: a struct with member initialisers used as
the default argument of a member function of the class enclosing it is refused by g++ 11 and
accepted by MSVC and clang, so the Mac and the Windows box were green and the Linux box was not.
The struct has a constructor now. Cases: `override-final`, `override-no-base-refused`,
`final-member-refused`, `final-class-refused`, `final-nonvirtual-refused`.

### `= default` and `= delete`, and the members [class.copy] deletes

**A special member written `= default` inside its class is the one the compiler would have
written**, so nothing is declared at the `= default`: the class records which member and under
which access, and `declareImplicitSpecials` writes it exactly as it would have - no symbol where
it is trivial, a synthesised body where it has work. **`= delete` is a mark on the signature**:
overload resolution ranks a deleted function like any other and refuses it only where it wins,
which is what lets `f(double) = delete` take a `2.5` away from `f(int)`. The uses that do not go
through resolution are refused where they are made - a by-value argument's copy constructor,
the address of the function (`&f` of a deleted `f` was a link error) - by the three functions
every such use already passed through.

**And the ones nobody wrote**: [class.copy]/7, /11 and /18 delete the implicit copies of a class
whose move is defaulted, or whose base or member has its copy deleted. They are declared deleted
rather than left undeclared, because an undeclared copy is how a trivially copyable class is
copied - the deletion would have become a byte copy. Measured against clang: `W b = a;` for a
`W` holding a `U` with a deleted copy is "call to implicitly-deleted copy constructor" there and
"is deleted - the compiler would write it" here, at the same column.

Refused by name: `= default` on a later declaration (user-provided there, where the implicit
member is what stands in here), a deleted destructor, and a deleted virtual function. Cases:
`defaulted-members` (a `.nonames` - a trivial defaulted constructor beside a written one is a
function here and nothing in clang), `defaulted-non-special-refused`, `deleted-*-refused`.

### A `constexpr` constructor runs at run time

**[dcl.constexpr]/3 lets every constexpr function run at run time**, and a constexpr constructor
is one: the keyword is stepped over and the class is built the ordinary way. What the constant
evaluator cannot do is build an object - it folds integers and floating values - so a
`constexpr` object of class type is refused by name, the spelling that works being `const`. A
statement in a constexpr constructor's body is C++14 and refused naming the version, which is
also clang's answer under `-pedantic-errors`. Cases: `constexpr-constructor`,
`constexpr-class-object-refused`, `constexpr-constructor-body-refused`.

### `enum class`, the type the enumeration already almost was

**An enumeration here was already a type that remembers its name** - `Kind::Int` underneath,
interned per tag, spelled by both manglers - so a scoped one needed no new kind: it is that type
marked scoped, and no backend and no mangler heard about it. What changed is the scope and the
conversions. A scoped enumerator is keyed `E::a` and nothing else, named plainly only inside its
own braces where [dcl.enum]/5 gives it the underlying type; `E::a`, `n::E::a`, `C::E::a` and
`T::a` through a template parameter all reach it, and so does `E::a` for an *unscoped* E, which
C++11 allows and which was "'E' was not declared". [dcl.enum]/10 and [conv.prom]/3 are the rest:
no promotion, no rank against any other parameter type, no implicit conversion in either
direction, no truth value - but the governing value of a `switch` - and no arithmetic; two of one
type compare and a cast, functional casts included, reaches the integer. The opaque declarations
`enum class E;` and `enum E : int;` declare the type before its body.

**The operator refusals live in another workstream's files**, because the built-in operators are
written there and nothing in the type can refuse an operand: six lines call
`refuseScopedOperand` and the enumerator lookup from `ParserExpr.cpp` and `ParserOperator.cpp`.
Over-accepted still: `case 1:` under a switch on a scoped enumeration, folded to a value before
its type is asked. Cases: `enum-class`, four `enum-class-*-refused`, and two overload files that
ask clang which function a scoped enumerator reaches.

### `friend class X;`

**[class.friend]/2: every member of X reaches what the class keeps private**, a nested class of X
included. The class records X's tag - an undeclared X being named in the namespace around the
class, [namespace.memdef]/3 - and `isFriendOf` asks first whether the code being read is inside
such a member, so all the access checks, which already asked `isFriendOf`, honour it without
learning anything. `friend X;` for a declared class is C++11's spelling and is read too.
Friendship is not inherited. Cases: `friend-class`, `friend-class-not-friend-refused`.
