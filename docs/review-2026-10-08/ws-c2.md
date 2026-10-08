# WS-C2 - class declarations, second half: handover

Branch `review/c2-class`, from main `0e64e73`, 2026-10-08. Stopped early on the main session's
instruction; nothing pushed or merged.

## Stopped here

**Done.**
- Windows box workspace `C:\cxx1\rtsdiv\ws-c2` (clone of `0e64e73`, `core.autocrlf=false`,
  compiler built by `msvc\build.cmd`); helper scripts in `C:\cxx1\rtsdiv\ws-c2-tools`
  (`build.sh`, `emit.sh`, `gold.sh`, `both.sh` - cpp11 beside clang -std=c++11 -pedantic-errors).
- **Emit goldens recorded from `0e64e73`'s compiler before any change**:
  `tests/out-emit.golden` 1602 files, `tests/out-emit-O2.golden` (tms6747 -O2) 402 files, both in
  the box workspace. Nothing has been compared against them yet.
- clang measured on the box for F07a and F10 probes (`ws-c2-tools\p\*.cpp`): unknown attributes,
  `gnu::`/`clang::` ones and attributes after a declarator-id, ptr-operator, array bound, class and
  enum head, alias name and parameter all accepted (warning only); `[[deprecated]]` refused as a
  C++14 extension (col 3); `[[noreturn(1)]]` refused ("cannot have an argument list");
  `using X = int y;` refused ("type-id cannot have a name"); `using X = static int;`,
  `using X = auto;` and a redeclaration to another type refused; a redeclaration to the same type
  and `using X = struct S {...};` accepted.

**Half done - commit `d4f838d`, WIP, never compiled.**
- F07a: `aliasDeclaration(prefix)` (ParserConst.cpp) replaces `refuseAliasDeclaration` at the three
  doors - class body (ParserType.cpp), block (one line in WS-D's ParserStmt.cpp) and file scope (one
  line in WS-D's ParserTopLevel.cpp); enters the same typedef table, refuses storage class, `auto`,
  a named declarator and a redeclaration to another type.
- F10: `skipAttributes()`/`skipBalanced()` (ParserType.cpp) read attributes at the decl-specifiers,
  the declarator (start, after `*`/`&`/`&&`, after the declarator-id, after each array bound -
  `arraySuffix` no longer reads `[[` as a bound), after a member's parameter list, at the start of
  the exception specification, and at class and enum heads. Statement attributes refused by name.
- Still to do for both: build, write `alias-declaration.cpp`/`attribute-ignored.cpp` and refusal
  cases with clang's output, rework `tests/cases/attribute.cpp`/`.error` (the message changed),
  update EXCLUSIONS.md (alias and attribute entries; the new statement-attribute refusal) and run
  `tools/exclusions --check`, `make comments`, the golden comparison and the gates.
- Note: `src/parser/ParserType.cpp` is CRLF in the repository (since before `2dd633b`); edits
  kept it CRLF.

**Untouched.** F16, F33, the F19 neighbour check, F08, F21, the F15 design note, D12, and the extra
item (destructor access for a local, a temporary, and members/bases destroyed by an implicit
destructor).

**Running on the Windows box: nothing.** Linux box not used.
