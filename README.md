# cxx1 — a C++11 compiler

`cxx1` compiles C++11 to native assembly for **x86_64-linux**,
**x86_64-windows** and **arm64-darwin**. It is written in ISO C++14 and
depends on no compiler framework: its own lexer, preprocessor, parser, type
system and three code generators.

It is early. `CLAUDE.md` opens with **"Reading this tree"**, which is the order
to read this one in - the ladder, the pipeline by file, a case per rung, and the
suites - and then holds the measurement behind each decision. What the compiler
does *not* accept is `docs/EXCLUSIONS.md`, a refusal site per entry, cited by
file and line.

## Build

```
make
```

Needs a C++14 compiler and nothing else. `clang++` on a Mac, `g++` on Linux;
both are checked, and `cl` is the third, through `msvc\build.cmd` on the
Windows box. `tools/verify-three` builds and tests on all three from the Mac.

## Use

```
cxx1 f.cpp -o f          compile, assemble and link for this machine
cxx1 -c f.cpp            stop at an object file
cxx1 -S f.cpp -o f.s     stop at assembly
cxx1 -S -arch x86_64-linux f.cpp
cxx1 -I dir f.cpp        add a directory to the ones <...> searches
```

`-D` defines a macro and `-U` removes one, `-j` sets how many files compile at
once, `-g` writes a line table for a debugger (x86_64-linux and arm64-darwin
only), `-masm` picks `masm` or `gnu` syntax for x86_64-windows, and `-time`
reports each phase. `cxx1` with no input prints the whole list.

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

**A subset ships, and it is a subset on purpose.** `include/` holds 27 C++
headers and `lib/` 18 C ones, a little over 2,200 lines together. They are here
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
| language support | `<initializer_list>` `<limits>` |
| C, inside `std` | `<cassert>` `<cctype>` `<cfloat>` `<climits>` `<cmath>` `<cstddef>` `<cstdio>` `<cstdlib>` `<cstring>` |

`lib/` holds 18 C headers - `assert.h` through `time.h` - and the nine `<c…>`
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
- The functions in `<algorithm>`, `<numeric>` and `<utility>` are `static`, so
  each translation unit gets its own copy.

### What is absent

**`<stdexcept>` is here as of 2026-09-07**, with the standard's nine classes
over a `std::string` and `std::exception` in `<exception>` beside it, as the
standard splits them. Nothing else in the library throws: a container that runs
out of memory or is indexed past its end does not raise, it misbehaves, and
`at()` **is** provided and does not throw - it indexes like `operator[]`, so a
past-the-end `at()` misbehaves rather than raising `std::out_of_range`. That is
the one place the sentence above is not kept, and it is worse than not providing
it: a caller who wrote `at()` for the check does not get one. `include/vector`.

`<memory>`, `<type_traits>`, `<new>`, `<typeinfo>`, `<functional>`, `<deque>`,
`<list>`, `<unordered_map>`, `<thread>` and the rest of C++11 are not present.
Two of those are held up by the language rather than by effort: `<new>` has
little to declare while an overloaded `operator new` is refused - so placement
new is refused with it, though plain `new` and `delete` work - and
`<typeinfo>` waits on `typeid`, which is refused by name. `dynamic_cast` does
work, and has since each vtable got a `type_info` beside it.

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

## Known shortcomings in 1.4

Everything here is measured and written down elsewhere in full; this is the
short list a user of **1.4** should have in front of them. `docs/EXCLUSIONS.md`
is the complete one, each entry citing the source line that refuses it.

**The language is a subset, and these are the parts most likely to be missed.**
`typeid` and `<typeinfo>`; an overloaded `operator new` or `operator delete`,
and placement new with them; a `goto` that leaves a `catch` handler; an alias
template (`template <class T> using X = ...`), a variable template and an
`inline` variable, the last two being C++14 and C++17; a ref-qualifier on a
member function; a scoped enumeration's `enum class` base clause; and raw
string literals. Plain `new`/`delete`, `dynamic_cast`, exceptions, templates
with partial specialization and lambdas all work.

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

**`-masm=masm` cannot link a program of more than one file**, and that is
ml64's limit rather than this compiler's. A member function defined inside its
class becomes a strong symbol in every object that includes the header - this
tree's own `<string>` defines its members that way - and folding them needs a
COMDAT section, which **ml64 has no directive for at all**. `link.exe` answers
LNK2005 for each duplicate.

Since 1.2 that is no longer the default: x86_64-windows assembles through the
**GNU spelling**, which clang turns into an ordinary MSVC-ABI COFF object and
which does mark each such definition COMDAT - the same thing cl's own objects
do. So a multi-file Windows project links. What it costs is a clang on that
machine, which Visual Studio 2022 ships as an optional component; `-masm=masm`
keeps the ml64 path for a machine without one, and `examples/unity.cpp` shows
the single-translation-unit build that path needs.

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
the four suites on macOS, the same on a Linux box with real g++, and the case
suite plus a name-by-name comparison against `cl` on a Windows box. `make test`
is the local half of that. The sources this release was built from are sealed -
see `README.1ST`, section 6.

## What it is not

**It cannot compile itself, and never will.** `src/` is C++14 and `cxx1`
accepts C++11, so the source is out of reach of the program by construction.
Correctness is established by differential testing against gcc, clang and cl
rather than by bootstrapping.

**It is not a conforming implementation**, and the headline says so: the
language is C++11 minus `docs/EXCLUSIONS.md`, with the library above rather
than the standard's. A refusal that names a feature is the intended answer; a
refusal naming nothing is a defect, and finding those is what the sweeps in
`CLAUDE.md` are.

## Where it came from

Forked from Compiler-C, a C90 compiler sharing the three code generators, on
2026-08-26.
