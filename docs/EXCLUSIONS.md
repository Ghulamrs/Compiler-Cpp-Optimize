# cpp11: what it does not yet do of ISO C++11

The banner says what cpp11 claims; this file says where the claim stops. Each item names the clause it
falls short of. The conformance suite (`tests/conformance`) and the library census (`tests/conformance/library`)
measure the rest.

What is implementation-defined - the locale names, the character sets, the freestanding C6747's headers,
the limits - is in IMPLEMENTATION-DEFINED.md, not here.

**The text that is meant** is ISO/IEC 14882:2011 as published, read with the resolutions of the Core and
Library issues that clang applies to a program compiled as `-std=c++11` - defect reports against C++11, not
the changes of C++14. That is how the suite's cases were judged (clang++ 22.1, `-std=c++11 -pedantic-errors`,
on cpp11's own headers), so where the published text and a resolved issue differ, the issue's answer is the
one cpp11 is measured against; CWG 1591 (deduction from a braced list to an array) is one such.

## Library

- **On the C6747, whose runtime is RTS6x**, nothing of what [compliance]/3 asks of a freestanding
  implementation is missing since cpp11 1.74: `typeid(*p)` with a null `p` throws `std::bad_typeid`
  ([expr.typeid]/2), the last of them (the compiler wrote no test of the pointer for this target until it was
  known that TI's own runtime, which a build may link in RTS6x's place, has `__cxa_bad_typeid` too - CCS 7.4's
  rts6740 has). `bad_cast` is thrown since RTS6x 1.3; `quick_exit` and `at_quick_exit` are there since 1.4,
  `_Exit` since before; `exception_ptr`, `current_exception` and `rethrow_exception` since 1.6.
- **A violated `throw(T...)` list or `noexcept` on x86_64-linux and arm64-darwin has not been run**
  ([except.unexpected], [except.terminate]). On x86_64-windows both are met, and on the C6747 since cpp11 1.71
  and RTS6x 1.5: `std::unexpected`'s handler is called once the function's objects are destroyed, what it throws
  is measured against the list (and becomes `std::bad_exception` where the list has it), and a violated
  `noexcept` calls `std::terminate`, whose handler the program may have set.
- **`what()` is declared without `noexcept`** in `std::exception` and the classes derived from it, where
  [exception] writes `virtual const char *what() const noexcept;`. Deliberately: [except.spec]/5, which cpp11
  applies, refuses an overrider that promises less, so with it a program's own `const char *what() const`
  - written without `noexcept` or `throw()`, as much C++03 code has it and as cl accepts it - would be refused.
  `noexcept(e.what())` is therefore false. The other members [exception], [bad.alloc], [bad.cast] and
  [type.info] write `noexcept` are so since 1.77.
- **The library's headers use names a program may define as macros** ([global.names] reserves `_Upper` and
  `__double` names for the implementation, and the headers' parameters and private members are not all so named):
  a program that defines a macro `V`, `count` or `first` before including a standard header can break that header.
  Seen in the sweep of 1.75 (a test's own `V`). The public names are the standard's; the internal ones want
  renaming, header by header.
- **A pointer caught by reference on the Itanium targets refers to a copy** of the pointer the runtime hands
  back, not to the exception object ([except.handle]/17): `catch (int *&p) { p = 0; throw; }` rethrows the
  original. The Microsoft runtime hands over the object itself.
- **On the C6747 a `thread_local` object has static storage** (IMPLEMENTATION-DEFINED.md), so its address is
  taken as a template argument there, which [temp.arg.nontype]/1 does not allow (r1n10 of the suite).
- **`vm6747` keeps subnormal values where the C674x and `sim6747` flush them to zero**: a program run on the
  emulator sees gradual underflow that `numeric_limits` says the C6747 does not have (`denorm_absent` since 1.83,
  as CCS 7.4's `<limits>` says). Run a program that depends on it on `sim6747`. Found by the user trial of Setup
  1.10.5; the trial's three faults of the library itself are mended in 1.83.

## Language

- **On x86_64-linux, a `long double` literal a `double` cannot hold exactly is refused** ([lex.fcon]): `0.1L`,
  `LDBL_MAX`. The compiler carries a floating constant in a double, so that every build machine reads the same
  number, and x87's 64-bit significand holds more than that.
- **A template name out of reach inside a template's own definition** is not refused, since a template's text is
  read when it is instantiated ([temp.res]); outside a template it is.
- **`p->One<int>::~One()` is accepted** where the name after `::~` should be found in the scope the name
  before it was found in, as a type ([basic.lookup.qual]/6: `~One<int>` there, or a typedef's name); cl accepts
  it too, and clang refuses it only as `-pedantic-errors` reads the text. The forms the text gives are all taken
  since 1.74 (the suite's dt01).
- **Itanium's `Tn` mangling prefix** for a function template whose parameter type depends on another is not
  produced (clang 17's mangling; [temp.over.link] is met, the names differ from clang's).
- **A member's `noexcept(e)` asked while its class is still being defined answers what an early reading
  gave** (CWG 1330). Since 1.80 the specification is read again when the class is complete, so it may name a
  member declared below it; but a constant expression inside the class body that asks it before then - a
  static member's initialiser `= noexcept(T().f())`, an array bound - sees the first reading, which is "may
  throw" where that reading failed.
- **`this->m` for a data member, in a trailing return type inside the class body, is "no member m"**
  ([expr.prim.general]/3): `auto g() -> decltype(this->m);` with the class not yet complete. `decltype(m)`,
  a member function called there by its bare name or through `this`, and all of these in a definition
  outside the class, are read (the functions since 1.80).
- **On x86_64-windows, a local aligned beyond 16 bytes in a function that has a try block or a local with a
  destructor is refused** by the code generator ("not supported yet"): `alignas(32) char buf[4];` beside a
  `std::string`. The frame is realigned for such a local, and a handler's funclet reaches the frame through
  the establisher, which does not know the realigned base. Put the aligned object in a function of its own.
  Found by 1.80's sweep of declarations.

**Found by the user trial of Setup 1.10.5** (2026-11-20: three users wrote 73 programs of their own, and each
shape was reduced from one of them and checked against clang - the trial's report is
`docs/reviews/GRIDE user trials - cpp11 1.80 - three Opus 5.5 users.html`). The 29 shapes of valid C++11 it found
refused, and its crash (`std::swap` of an unnamed union), are accepted since 1.82 - the suite's p182 holds them.
What is left of them:

- **A `?:` whose arms are class values does not fold in a constant expression** ([expr.const]/2):
  `constexpr P pw(int n) { return n == 0 ? P(1) : P(pw(n - 1).r * 2); }` compiles and runs, but
  `static_assert(pw(5).r == 32, "")` is refused as not a constant. A class value made any other way folds,
  through a recursive constexpr function too.
- **A using-directive naming a nested namespace** (`using namespace a::c;` written in `a::b`) still hides the names
  of the namespaces around the call that [namespace.udir]/2 would join with it; a directive naming a namespace of
  the global namespace joins its names with the global ones, wherever it is written.
- **A typedef of a specialization over a class still incomplete waits for a use** at namespace and block scope
  only ([temp.inst]/1); a member typedef inside a class body makes the specialization at once.
## Permitted, and not what cl and clang do

- **Where a copy [class.copy]/31 lets an implementation leave out is still made, and where cpp11 leaves out one
  cl makes.** Since 1.78 a temporary of a class is the object it initialises - a local, a by-value argument, the
  returned object, a file-scope or static object, an array's element, a member, the object of a new-expression -
  as cl and clang have it. Still copied: the temporary an arm of `?:` makes (`K v = c ? K(1) : K(2);` copies
  once, as cl; clang copies none) and a temporary of a derived class into a base. **A named local returned** is
  the caller's object since 1.81 where every `return` of its scope names it, as clang and cl have it; where the
  scan of the tokens cannot tell (a `return (x);`, the name written again as what looks like a declaration, a
  `return` of something else in its scope), it is built in the frame and moved to the caller's by its move
  constructor, as [class.copy]/32 has it. Until 1.81 it was moved by its bytes, which no constructor permits.
  A program that counts its copies and moves prints other numbers in those shapes; none is wrong.
