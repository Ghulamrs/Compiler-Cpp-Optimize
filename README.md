# cxx1 — a C++03 compiler with C++11 extensions

`cxx1` (`cpp11.exe`) compiles **C++03 with C++11 extensions** to native
assembly for **x86_64-linux**, **x86_64-windows**, **arm64-darwin** and
**tms6747** (TI's C6747 DSP). It is written in ISO C++14 and depends on no
compiler framework: its own lexer, preprocessor, parser, type system and four
code generators.

**The headline is measured, and it changed on 2026-10-08.** Every document in
this tree used to say "C++11 minus `docs/EXCLUSIONS.md`"; an independent review
that day put 122 C++11 feature probes through the compiler and 56 were refused -
`enum class`, `override`, `= default`, delegating constructors, alias
declarations, forwarding references and list-initialisation among them - which
is roughly half of what C++11 added to C++03, and under a fifth of its library.
What *is* here of C++11: `auto`, `decltype`, lambdas with every capture, rvalue
references and move, variadic type packs, `static_assert`, `constexpr`
functions, `nullptr`, range-based `for`, `noexcept`, `explicit` conversion
functions, `char16_t`/`char32_t`, `alignas`. The complete list of what is not
is `docs/EXCLUSIONS.md`, a refusal site per entry, cited by file and line and
checked against the source by `tools/exclusions --check` on every `make test`.
The headline goes back to "C++11" when that list says it may.

`CLAUDE.md` opens with **"Reading this tree"**, which is the order to read this
one in - the ladder, the pipeline by file, a case per rung, and the suites - and
then holds the measurement behind each decision.

## Build

```
make
```

Needs a C++14 compiler and nothing else. `clang++` on a Mac, `g++` on Linux;
both are checked, and `cl` is the third, through `msvc\build.cmd` on the
Windows box. `tools/verify-three` builds and tests on all three from the Mac,
and runs the tms6747 leg on TI's own simulator from the Windows box.

## Use

```
cxx1 f.cpp -o f          compile, assemble and link for this machine
cxx1 -c f.cpp            stop at an object file
cxx1 -S f.cpp -o f.s     stop at assembly
cxx1 -S -arch x86_64-linux f.cpp
cxx1 -I dir f.cpp        add a directory to the ones <...> searches
```

`-D` defines a macro and `-U` removes one, `-j` sets how many files compile at
once, `-g` writes a line table for a debugger - DWARF on x86_64-linux and
arm64-darwin, CodeView on x86_64-windows through the `gnu` spelling (since
1.6); tms6747 writes none and `-g` is refused there - `-masm` picks the assembly syntax for
x86_64-windows (`masm` for the project's own masm and link, the default where
they sit beside the compiler, `gnu` for clang, the default elsewhere and for
`-g`, `ml64` for Microsoft's assembler, one translation unit per program),
`-O1`/`-O2`/`-Os` optimise, and `-time` reports each phase. `cxx1` with no
input prints the whole list, and `-version` the release and its seal file.

Targets are named `x86_64-linux`, `x86_64-windows`, `arm64-darwin` and
`tms6747`. A host target that is not this machine implies `-S`: the compiler
will not ask the local assembler to build for hardware it is not on. `tms6747`
is different, since no host is a C6747: `-c` assembles what the compiler wrote
with asm6x, the project's own C6000 assembler (`CPP11_AS` names it, else the
one beside `cpp11`, else `asm6x` on PATH), into a TI object on any machine;
without `-c` the objects go to TI's `lnk6x` against `rts6740_elf.lib` - or
`rts6740_elf_eh.lib`, which a C++ program that throws needs - into a `.out`,
which needs CCS: `CPP11_TI` names its C6000 compiler directory, `CPP11_TILIB` a
directory holding the exception-handling runtime, `CPP11_LD` the linker itself.
`tests/asm6x.sh` holds every case's driver-made object to asm6x's own;
`tools/windows/ti-link.cmd` links every case on the box, and `verify-three`
runs both.

## The library

**A subset ships, and it is a subset on purpose.** `include/` holds 31 C++
headers and `lib/` 19 C ones (counted 2026-10-08). They are here
for two reasons: the core language cannot do without some of them -
`<initializer_list>` is what a braced initialiser *means* - and a compiler with
no containers cannot be pointed at a real program. **They are not an
implementation of the C++11 library and are not becoming one.**

| | |
| --- | --- |
| containers | `<vector>` `<string>` `<map>` `<set>` |
| algorithms | `<algorithm>` `<numeric>` `<utility>` |
| streams | `<iostream>` `<ostream>` `<istream>` `<sstream>` `<fstream>` `<ios>` `<iomanip>` |
| exceptions | `<exception>` `<stdexcept>` |
| language support | `<initializer_list>` `<limits>` `<new>` `<typeinfo>` `<type_traits>` |
| C, inside `std` | `<cassert>` `<cctype>` `<cfloat>` `<climits>` `<cmath>` `<cstddef>` `<cstdint>` `<cstdio>` `<cstdlib>` `<cstring>` |

`lib/` holds 19 C headers - `assert.h` through `time.h` - and the ten `<c…>`
names forward into it. **The forwarding is a using-declaration**, `namespace std
{ using ::size_t; }`, rather than a repeated typedef: a typedef would be a
second type that happened to agree, and the two would differ to an overload.
Measured complete - every standard name `stdio.h`, `stdlib.h`, `string.h`,
`ctype.h` and `math.h` declare is named in the `<c…>` beside it, and the only
names left behind are the platform's own, `fileno` and `__acrt_iob_func`, which
do not belong in `std`. `<cassert>`, `<cfloat>` and `<climits>` forward nothing
and are right not to: what they provide is macros, and a macro belongs to no
namespace.

**How a header is found.** `<...>` searches every `-I` directory in the order
written, then `include/`, then `lib/`. C++ comes before C because `<cstddef>`
has to be found before it can include `<stddef.h>`. Both directories are
compiled into the binary as absolute paths - `$(CURDIR)/include` and
`$(CURDIR)/lib` at build time - so a `cxx1.exe` copied elsewhere still reads
the headers of the tree it was built in, and stops finding them if that tree
moves.

### What these headers are honest about

Every one of them documents its own limits at the top. The ones that change
what a program *means*:

- **`std::cout`, `cerr`, `clog` and `cin` are `static`** - one set per
  translation unit rather than one per program. A header-only library has
  nowhere to put a definition. They also have no constructor: `ostream` is an
  aggregate built from constant initialisers, and the `FILE *` is resolved at
  the point of use, so nothing has to run before `main`.
- **`vector<T>::iterator` is `T *`**, not a class. That is what keeps the header
  short - `++it`, `*it`, `it->m` and `it != v.end()` are all built in - and the
  cost is that any insertion invalidates every iterator, `begin()` on an empty
  vector answers null, and a `vector<int>` iterator compared against a
  `vector<char>` one is not caught.
- **`vector` drops a slot without destroying it** on `pop_back`, `clear` and
  `erase`. Correct for `vector<POD>` and `vector<T *>`; a
  `vector<std::string>` leaks. Its storage is `calloc`'d and assigned into,
  because placement new is refused by this compiler, so a zeroed slot is a real
  state a class held here has to tolerate.
- **`map` and `set` are sorted vectors**, binary-searched. The interface is the
  standard's and the complexity is not: an insertion moves elements.
- **`sort` is an insertion sort** - quadratic, no scratch memory, and it steps
  the iterator both ways, which is all any iterator here can promise.
- **`numeric_limits` means something only where it is specialised.** The primary
  template answers `T()` to every query.
- The library's free functions are `inline` and its function templates plain,
  as the standard has them - one definition merged across translation units
  (since 2026-09-19; they were `static` while cxx1 had no weak linkage). Only
  the four stream objects in `<iostream>` stay `static`: an `inline` variable
  is C++17, and there is no object file to define them in.

### What is absent

**`<stdexcept>` is here as of 2026-09-07**, with the standard's nine classes
over a `std::string` and `std::exception` in `<exception>` beside it, as the
standard splits them. Nothing else in the library throws: a container that runs
out of memory or is indexed past its end does not raise, it misbehaves, and
`at()` **is** provided and does not throw - it indexes like `operator[]`, so a
past-the-end `at()` misbehaves rather than raising `std::out_of_range`. That is
the one place the sentence above is not kept, and it is worse than not providing
it: a caller who wrote `at()` for the check does not get one. `include/vector`.

`<memory>`, `<functional>`, `<tuple>`, `<array>`, `<deque>`, `<list>`,
`<unordered_map>`, `<unordered_set>`, `<thread>`, `<atomic>`, `<mutex>`,
`<chrono>`, `<regex>`, `<random>`, `<cstdarg>` and the rest of C++11 are not
present. `<new>`, `<typeinfo>` and `<type_traits>` are: a class's
own `operator new` and `operator delete`, the placement and nothrow forms,
`std::bad_alloc` from the platform runtime, `typeid` and `dynamic_cast` all
work - the sentence that stood here until 2026-10-08 saying the first two were
refused was stale by a month, and the review of that day is what caught it.

## What 1.4 changes

The cl review of 2026-09-17 (`VM6747/CXX1I-REVIEW-2026-09-17.md`, made against
`cl.exe` where every earlier review had been made against clang) found nine
wrong answers, every one at the boundary with cl-compiled code or in layout
and so invisible to a program compiled entirely by cxx1. All nine are fixed
in Compiler-Cppi and cherry-picked here, six commits, verified on the Mac,
the Linux box and the Windows box - where a new pair suite, `tests/winlink/`,
links each half of a program compiled by cxx1 against the other compiled by
cl, both ways. On x86_64-windows: a constructor returns `this` in RAX, which
cl's callers use; a class's own overloads of one virtual name take cl's
vftable slots; a prototype's by-value parameter no longer leaks into the next
definition's destructor list; two adjacent empty bases get cl's byte between
them; `#pragma pack` is honoured; a pointer to a member is sized by cl's
inheritance model (4/4/8 and 8/16/16). On every target: an empty base goes
where Itanium puts it too (at 0 unless a same-type empty subobject is there);
`int B::*` converts to `int D::*` ([conv.mem]); a null data member pointer is
-1, not 0; `<ostream>` prints `signed char` and `unsigned char` as
characters; `<istream>` stops a number at its last digit and stores 0 on
failure; unary `+` promotes; and an override must keep the return type, or
return a covariant pointer or reference (cl's C2555 otherwise). Re-sealed the
same day with two small fixes: the driver finds its include directory by
`cstddef` rather than by a C header, so an installation whose C headers sit
in a `lib/` beside it - RIDE's, cc1's - is found; and an unreachable `return`
after a diagnostic is gone (cl's C4702).

## What 1.3 changed

Seven fixes and one region, every one cherry-picked from Compiler-Cppi - the
clone that carries this compiler's fourth target - and verified here on the
Mac, the Linux box and the Windows box: a mem-initialiser may name a
template-id base (`: P<int>(x)`); a lambda's body sees the statics,
enumerators, typedefs and nested classes of the class it was written in; a
lambda's return type is deduced from every `return` in its body, through a
`try` or an `if`; a floating constant expression folds (`constexpr double`, a
`static_assert` over one, an array bound cast from one); a static local is
named by its function's mangled symbol, so two functions of one name, an
overload, a destructor and a lambda may each have one; and two terminate
scopes on the Itanium targets - a destructor that throws while an exception
unwinds, and a by-value `catch` whose copy throws, both call `std::terminate`
as the standard says. `tests/open` holds the two Itanium virtual-inheritance
defects below and nothing else; Compiler-Cppi has closed those too, with the
vtable group, `_ZTv` thunks, VTT and construction vtables, and that work is
not in 1.3.

## Known shortcomings in 1.6

Everything here is measured and written down elsewhere in full; this is the
short list a user of **1.6** should have in front of them. `docs/EXCLUSIONS.md`
is the complete one - 131 refusal sites on 2026-10-08, each citing the source
line that refuses it, and `make test` fails when the two disagree. The heading
above is written by `tools/version-lines` from `src/Version.h`; it said 1.4 for
two releases before that tool existed.

**The language is C++03 with C++11 extensions, and these are the C++11 parts a
real codebase meets first.** `enum class`; `override` and `final`; `= default`
and `= delete`; a delegating constructor; `using X = T;` and an alias template;
a forwarding reference - `template <class T> void f(T &&)` deduces for an
rvalue only; list-initialisation that calls a constructor, `P p{1, 2}`,
`T{...}` as an expression, `new T{...}`; a trailing return type; a `constexpr`
constructor; a raw string literal; a user-defined literal; `thread_local`; an
inline namespace; `extern template`; a template template parameter; a non-type
parameter pack; a capture-less lambda converting to a function pointer; a
ref-qualifier on a member function; a `goto` that leaves a `catch` handler; a
function try block and an anonymous union, which are C++98. What works, and is
measured against clang in the suites: `auto`, `decltype`, lambdas with every
C++11 capture, rvalue references and move, variadic type packs,
`static_assert`, `constexpr` functions, `nullptr`, range-based `for` over an
array or a pointer-iterator container, `noexcept`, `explicit` conversion
functions, `alignas`, `char16_t`/`char32_t`, `dynamic_cast`, `typeid`, a
class's own `operator new`, exceptions with class types, virtual inheritance on
the Itanium targets, partial specialization, SFINAE, member and constructor
templates.

**Templates instantiate by replaying tokens**, so every name in a template body
is looked up at instantiation - MSVC's old model. cxx1 therefore *accepts*
some programs clang refuses, which `docs/CONFORMANCE.md` records rather than
hides. Four shapes met while writing `<type_traits>` are still refused:
`template <class T, T v>`, a typedef naming a template-id used as a base, a
partial specialization on `X<T []>`, and an out-of-line constructor of a class
template.

**Pointers to members** stop short of two things: a pointer to a *virtual*
member function is refused by name (Compiler-Cppi has it), and `&D::f` for an
`f` that D inherited is not found - write `&B::f`, which converts.

**x86_64-windows lags the two Itanium targets on exceptions and layout**, and
each refusal names itself:

* a destructible local and a `try` in one function, function-wide;
* `return`, `break` or `continue` that leaves a `catch` - a handler is a
  funclet there;
* a `try` inside another; a class-typed `throw`; a rethrow from inside a
  handler;
* an override from a base that is not the first, where cl biases `this`
  instead of emitting a thunk;
* a `volatile` object with external linkage, whose name cl decorates.

**Virtual bases are no longer among them.** The Microsoft layout - the vbptr,
one vbtable per pointer, the store in every constructor and both walks that
read one - landed on 2026-09-10 and matches `cl` byte for byte, diamonds
included; so did cl's hidden most-derived flag, which is how that ABI builds a
virtual base once rather than once per class that names it. What remains is
three defects the work uncovered, all of them **Itanium-side or shared** and
all registered in `tests/open`: a second base's own virtual base is read
through the wrong pointer, and `delete` through a pointer to a polymorphic
virtual base misses the most-derived destructor. Two more that the work found
are **fixed**: the first non-virtual base carrying a vptr is laid at offset 0
on the Itanium targets whatever its place in the base-clause, and a class that
introduces its own vptr puts it in front of every base rather than over the
first one's members - with `__vmi_class_type_info` beside it, so that a
`catch` by a base at a non-zero offset lands where the object is.

**That Windows defect is closed as of 1.2**: a function other than `main` that
owned an unwind region used to emit a `.pdata` entry pointing at a `$cppxdata`
label the compiler never laid down, and `ml64` answered
`A2006: undefined symbol`. Both spellings take the answer from one place now,
and `by-value-parameter-unwind.cpp` is the case that keeps it that way.

**`-masm=ml64` cannot link a program of more than one file**, and that is
ml64's limit rather than this compiler's. A member function defined inside its
class becomes a strong symbol in every object that includes the header - this
tree's own `<string>` defines its members that way - and folding them needs a
COMDAT section, which **ml64 has no directive for at all**. `link.exe` answers
LNK2005 for each duplicate.

The two other spellings both mark it: `gnu`, which clang turns into an ordinary
MSVC-ABI COFF object (the default since 1.2 where no masm sits beside the
compiler, and what `-g` needs, CodeView living in that spelling alone), and
`masm`, which the project's own masm assembles with the COMDAT written as cl
writes it and the project's link links (the default in RIDE's `bin`, where both
sit beside `cpp11.exe`). So a multi-file Windows project links through either.
`-masm=ml64` is for a machine with neither clang nor the project's masm, and
`examples/unity.cpp` shows the single-translation-unit build it needs. The
`masm` spelling's own gaps - ten of 379 cases refused by the project's masm on
2026-10-08 - are workstream F's in `docs/REVIEW-PLAN-2026-10-08.md`.

This is still why most `.nocl` and `.nonames` entries in `tests/cases/` exist:
cl folds an inline definition into a COMDAT and drops it when nothing calls it,
where cxx1 emits it.

**Five programs are known to answer differently from clang**, kept as programs
in `tests/open/` with clang's answer beside each: a `catch` parameter whose
copy constructor throws should call `std::terminate` and does not; a
`constexpr double` does not fold; a lambda body naming a class's `static`
member or enumerator is refused; a lambda whose only `return` is inside a `try`
deduces `void`; and a mem-initialiser naming a template-id base -
`D(int x) : P<int>(x) {}` - does not parse. `make open` runs them and gates
nothing.

**Two smaller ones worth knowing.** A static local's symbol is spelled
`f.name` rather than the Itanium `_ZZ1fvE4name` - nothing links against it, a
static local having no linkage, but a debugger will show the other name. And
`Driver::runTool` names a temporary and then renames it, which on the Windows
box can collide in a one-shot multi-file build: compile with `-c` and link
separately if that appears.

**What is verified, and where.** Every release is held to `tools/verify-three`:
the four suites on macOS, the same on a Linux box with real g++, the case suite
at -O0 and -O2 plus a name-by-name comparison against `cl` on a Windows box, and
the C6747 leg on TI's own simulator. `make test` is the local half of that, and
on the Windows box `tools/windows/names-overload.cmd` runs the two clang-asking
suites under Git's bash with Visual Studio's clang. The sources this release was
built from are sealed - see `README.1ST`, section 6.

**The four suites are four different questions, and the numbers mean different
things.** `tests/run.sh` compiles and runs every case on this machine: on
2026-10-08 that is 628 cases, of which **402 have a recorded output** and must
print it, and **226 have a recorded refusal** and must stop with that message -
the second number is the compiler saying no where it should, which proves the
refusal names the feature and nothing about what compiles; `run.sh` prints the
two apart. `tests/emit.sh` compiles every case for all four targets and stops at
assembly; its *golden* is a recording of what this tree emitted before a change,
read file by file after it, and **it is not an oracle** - it never fails the run
and says only what moved, which is what a refactor needs and no correctness
claim may rest on. `tests/names.sh` and `tests/overload.sh` ask clang on every
run, and are the two that compare this compiler against another. The optimizer
has no IR verifier; what gates it is `run.sh` at -O2 on the Linux box and the
Windows cases at -O2 (since 2026-10-07), `tools/identical.sh` across levels, and
`CPP11_C6XLIVECHECK` on the C6000 - a `Mir` consistency check (every use after
its definition, every label a target) is a later round, recorded in
`docs/review-2026-10-08/ws-a.md`.

**What the review of 2026-10-08 measured, for the record.** The compiler builds
clean from `src/` with g++ 11 under `-Wall -Wextra -Werror -pedantic`; its
x86_64-linux assembly for about 70 probe programs and a 16-file, 14,000-line
project assembles with clang and links with lld; its arm64-darwin output,
transliterated to ELF, runs correctly on real AArch64 hardware for about 60
probes - lambdas, virtual, multiple and virtual inheritance, RTTI, member
pointers, templates, nested exceptions, rethrow and cleanups; its tms6747
output goes through the project's own `asm6x` for 397 of 397 suite cases; and
`-j` is deterministic, byte-identical output on all four targets whatever the
thread count. The same review looked for fabricated output and for a silent
fallback to cl, gcc, ml64, cl6x or lnk6x in this compiler and its seven sibling
tools and found none - zero `system()`, `popen()` or spawn sites in their
sources; the one "use the vendor's tools?" fallback in RIDE is prompted, logged
and refused in a headless build.

## What it is not

**It cannot compile itself, and never will.** `src/` is C++14 and `cxx1`
accepts C++03 with C++11 extensions, so the source is out of reach of the
program by construction. Correctness is established by differential testing
against gcc, clang and cl rather than by bootstrapping.

**It is not a conforming implementation**, and the headline says so: the
language is C++03 plus the C++11 features `docs/EXCLUSIONS.md` does not list,
with the library above rather than the standard's. A refusal that names a
feature is the intended answer; a refusal naming nothing is a defect, and
finding those is what the sweeps in `CLAUDE.md` are.

## Where it came from

Forked from Compiler-C, a C90 compiler sharing the three code generators, on
2026-08-26.
