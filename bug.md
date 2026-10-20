# bug.md

Open items found on 2026-10-20 while checking whether GRIDEd's `samples/cppparse` builds with cpp11.
Measured against cpp11 1.21 (61d34cb), built on Linux with `make`. Nothing in the source has been changed for
these yet.

| # | Item | Size | Status |
|---|---|---|---|
| B1 | `if (T(x) == ...)` is read as a declaration | small | open, cause located |
| B2 | Library pieces cppparse needs are missing from `include/` | small to medium | open |

The c90 side of the same check - c90 keeping its own copies of `src/backend/Dwarf.*` and
`src/backend/Backend.cpp` - is recorded in Compiler-Ci's `bug.md`.

---

## B1 - `if (T(x) == ...)` is read as a declaration

**Symptom.** A condition that opens with a functional cast is parsed as a declaration of the name in the
parentheses.

```cpp
#include <string>
int main() { const char *b = "}"; if (std::string(b) == "}") return 1; return 0; }
```

```
error: this condition needs a number or a pointer, not 'class basic_string<char>'
```

Seen in cppparse at `frontend/CxxParser.cpp:89`. g++ and clang (`-std=c++11 -pedantic-errors`) accept it.

Related forms, same root:

| Source | cpp11 1.21 |
|---|---|
| `if (std::string(b) == "}")` | "this condition needs a number or a pointer" |
| `if (std::string(b) == s)` | same |
| `while (std::string(b) == "}")` | "'b' is declared in a condition and has no initialiser" |
| `if ((std::string(b)) == "}")` | "expected an expression" - a second, separate case |
| `bool r = std::string(b) == "}";` | accepted (not a condition) |
| `if (std::string(b).size() == 1)` | accepted (`.` after the `)` is already handled) |

**Cause.** `if` and `while` decide between a declaration and an expression with `atDeclarationStart()`
(`src/parser/ParserStmt.cpp:2123` for `if`, `:2181` for `while`). That function rules out only `T(x).f()` and
`T(x)->f()` (`atTemporaryMemberAccess()`, `src/parser/Parser.cpp:675-701`). By [stmt.select]/2 a condition
declares only when an `=` or a `{` follows the declarator, so anything else after `T(...)` - `==`, `!=`, `<`,
`&&`, `)` and the rest - makes it an expression. `Parser.h:2326` already declares
`bool atConditionDeclaration() const;`, but nothing defines or calls it.

**Fix.**
1. Factor the scan in `atTemporaryMemberAccess()` into a helper that returns the token after `T(...)`.
2. Define `atConditionDeclaration()`: `atDeclarationStart()`, and when a `T(...)` group opens the condition,
   only if `=` or `{` follows it.
3. Use it at `ParserStmt.cpp:2123` and `:2181`.
4. Add the forms in the table above to `tests/cases` with their expected output.

The parenthesised form `if ((std::string(b)) == "}")` is a separate path (a parenthesised functional cast
taken for a C-style cast) and needs its own look.

**Check.** `make test` - baseline on Linux is 834 passed, 4 failed; the 4 fail only because the container's
shell prints `Aborted` after `abort()`, and they fail the same way before the change.

---

## B2 - Library pieces cppparse needs are missing

cppparse's 39 sources are strict C++11 (g++ and clang, `-std=c++11 -pedantic-errors`, 39/39 clean), but cpp11
1.21 compiles 6 of them. The first error of each failing file, by cause:

| Missing piece | Standard | Files stopped | Example in cppparse |
|---|---|---|---|
| `<iosfwd>` | [iosfwd.syn] | 13 | `analyzer/Ast.hpp:14` |
| `<ctime>` (`lib/time.h` exists; the C++ wrapper does not) | [ctime.syn] | 3 | `common/Parallel.cpp:4` |
| `basic_string::compare(pos, n, const char *)` | [string.compare] | 4 | `analyzer/Lexer.cpp:202` |
| `basic_string::rfind(const char */string)` and `rfind(s, pos)` (only `rfind(char)` exists) | [string.rfind] | 3 | `analyzer/Types.cpp:170`, `analyzer/ProjectReader.cpp:29` |
| `basic_string::insert(iterator, char)` | [string.insert] | 3 | `analyzer/Instantiate.cpp:34`, `backend/Mangle.cpp:63` |
| `vector(InputIt first, InputIt last)` | [vector.cons] | 2 | `frontend/SemaExpr.cpp:666`, `analyzer/Parser.cpp:724` |
| `ios_base::setf` (on `ostringstream`) | [fmtflags.state] | 2 | `analyzer/report/Diagram.cpp:43` |
| `<sys/stat.h>` | POSIX, not ISO C++ | 1 | `common/PathUtil.cpp:8` |

Each file stops at its first error, so more may be behind these. Adding the pieces and re-running the 39 files
shows the next layer.

`override`, `= default` and `= delete` are **not** gaps: they compile in 1.21.

**Fix.** Add the headers and members to `include/` (`string`, `vector`, `ios`), list what is still missing in
`docs/EXCLUSIONS.md` against N3337's clause names, and treat "all 39 cppparse files compile" as the acceptance
test. `<sys/stat.h>` is outside ISO C++ - decide whether cpp11 ships it in `lib/`.

The C++11 reference text: N3337, `git clone --branch n3337 https://github.com/cplusplus/draft`.
