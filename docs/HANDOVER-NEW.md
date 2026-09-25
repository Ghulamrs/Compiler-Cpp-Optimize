# Handover: the explicit destructor call, `<new>`, placement arrays - 2026-09-26

Base `4861997` on `gcc-scheme`; work on this worktree's branch, two commits,
not pushed and not merged. The CLAUDE.md section "The explicit destructor
call, `<new>` over the runtime, and placement arrays, 2026-09-26" is the
record of what was measured; this file is the gate numbers.

## Gates, each the run's own last line

Base, at `4861997`, clean build (`make clean && make`):

    tests/emit.sh --record   emit.sh: golden recorded, 1305 files
    tests/run.sh             run.sh: 532 passed, 0 failed
    tests/names.sh           names.sh: 336 passed, 0 failed
    tests/overload.sh        overload.sh: 30 agreed with clang, 0 differed
    tests/tms6747.sh         tms6747.sh: 325 passed, 0 failed, 0 skipped for exceptions, 7 skipped for a 64-bit long, 4 not for this target
    make comments            comment-lines: 63 group(s) over the cap
    tools/exclusions --check 136 sites, 127 uncited, 118 stale citations

After commit 1 (the explicit destructor call), measured by the coordinator on
a clean rebuild: run 533/0; emit 0 of 1305 changed, 4 added; overload 30/0;
tms6747 326/0; names 336 passed, 1 failed until the `.nonames` was written,
337/0 with it; comments 63.

After commit 2 (`<new>`, placement arrays, the key-function rule, three
`std` manglings), the compiler rebuilt (incremental, then the gates):

    tests/emit.sh            emit.sh: golden - 69 of 1305 files changed, 13 added, 0 removed
    tests/run.sh             run.sh: 536 passed, 0 failed
    tests/names.sh           names.sh: 340 passed, 0 failed
    tests/overload.sh        overload.sh: 30 agreed with clang, 0 differed
    tests/tms6747.sh         tms6747.sh: 327 passed, 0 failed, 0 skipped for exceptions, 7 skipped for a 64-bit long, 6 not for this target
    make comments            comment-lines: 63 group(s) over the cap
    tools/exclusions --check 135 sites, 124 uncited, 116 stale citations

The Windows box: `explicit-destructor-call`, `placement-array-new` and
`new-nothrow` assembled by clang, linked with libcmt and libcpmt and run there
through `tools/windows/gnu-run.cmd`, each line for line its `.expected`.
`tools/verify-three` was not run this round; the Linux box was not reachable
from this session (its key is outside what the session may read), so the
libstdc++ half of "the runtime exports these names" rests on that library's
export list rather than on a link on the box - `verify-three linux` is the
measurement to take before merging.

## The 69 changed emissions, explained

Every changed file is a case that includes `<string>`, `<stdexcept>` or a
stream header - 18 cases, on up to four targets:
include-c-wrappers include-map-set include-streams include-string
inline-overlaid-frames multiple-declarators range-for-vector runtime-shapes
stdexcept stream-base-manipulators stream-char-types stream-long-long
stream-number-stops string-at template-nontype-numeric temporary-of-template
vector-range-insert vector-value-init. Classified by
`tests/out-measure/classify2.sh` (a scratch script, not committed): of the
26,707 changed lines on x86_64-linux 25,344 name a `std` symbol or a string
literal; the 1,363 others are 1,232 removals and 131 additions. The removals
are the bodies of `std::exception::what()`, its D1, D2 and synthesised D0,
which every unit used to emit and which the key-function rule now takes from
the runtime; the additions are the respelled static `std::cout` and friends
(`_ZStL4cout` for `_ZN3stdL4coutE`, which the classifier's pattern did not
catch) and diff alignment. arm64-darwin and tms6747 read the same; on
tms6747 `std::exception` stays self-contained, so only the respellings and
the `_ZTS` string bytes moved there. x86_64-windows is the one target with
substantive additions - 3,045 lines - and they are `std::exception` in
MSVC's layout: the `__std_exception_data` member, the copy through
`__std_exception_copy`, the destructor through `__std_exception_destroy`.
No instruction outside those functions moved on any target.

## What is still refused, and why

- **A class's own `operator new[]` / `operator delete[]`**: `new T[n]` never
  consults the class. Named at the declaration.
- **A user placement form with more than one argument**: `new (a, b) T`.
- **`std::bad_alloc` caught on x86_64-windows**: a class is neither thrown nor
  caught on that target, the Microsoft ThrowInfo chain for a class being its
  own step. `new-header.notarget` says so; `new-nothrow.cpp` is the half that
  runs there.
- **The new handler, the nothrow allocator and the runtime's `bad_alloc` on
  tms6747**: the emulator's runtime has none of them (`<new>` declares them
  as rts6740 exports them); `new-header` and `new-nothrow` carry a
  `.notarget`, `placement-array-new` runs there.
- **[expr.new]/20's cleanup** - freeing the storage when a constructor throws
  inside a new-expression - is written for no form of `new`; clang names
  `_ZdlPvRKSt9nothrow_t` for it and the `.nonames` files record that.

## Open beside this, found and not taken

- `docs/EXCLUSIONS.md` has 124 uncited sites and 116 stale citations that
  predate this round (127/118 at the base); only the rows this round made
  stale were touched, as the brief asked. Regenerating it is its own change.
- `?:` types `const char * const` against `const char *` as incompatible
  (`include/exception` had to write an `if` for what `what()` wants to say);
  [expr.cond] makes that a legal expression.
- A `throw()` specification on a function that can throw nothing makes cpp11
  wrap it in a terminate scope with an `abort` call; correct, and code clang
  does not emit.
