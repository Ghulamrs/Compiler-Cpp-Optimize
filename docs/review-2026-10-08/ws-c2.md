# WS-C2 - class declarations, second half: handover

Branch `review/c2-class`, from main `0e64e73`, 2026-10-08. Stopped early on the main session's
instruction; nothing pushed or merged.

## Stopped here (second time, 2026-10-09 ~07:30)

**The Windows box was unreachable for the whole session** (host down from ~06:55) and the weekly
budget was at 92%, so nothing here has been compiled, run or measured against clang. Everything
below is written on the Mac and committed as **WIP, not built**; the coordinator's reduced scope
(F07a, F10, F16, F33, then stop) is what it covers.

**Written, not built - commit on top of `d4f838d`:**
- F07a finished: `aliasDeclaration` takes a function type (`using Fn = int(int);`) through
  `atParenInitialiser` + `typedefFunctionSuffix`; the file/block/class doors are as `d4f838d`
  left them. Cases `alias-declaration.cpp` (needs its `.expected` from clang),
  `alias-declaration-{named,storage,redeclared}-refused.cpp/.error` (clang columns written in
  the comments from the probes of 10-08, to be confirmed). `alias-declaration-refused.*` deleted.
- F10 finished: `attributedDeclarationAhead()` (ParserType.cpp) is called first in
  `statementBody` - `[[x]] int y;` in a block goes to `declaration()`, `[[x]] stmt;` is refused by
  name (one site; the duplicate fallback in `unqualifiedSpecifiers` is gone);
  `lastAttributeEnd_` is reset at the start of the specifier loop so a replay cannot meet a stale
  mark. Cases `attribute-ignored.cpp` (needs `.expected`), `attribute.cpp/.error` reworked to the
  C++14 message, `attribute-statement-refused.*`, `attribute-noreturn-args-refused.*`.
  Unverified risks: a free function's post-parameter attribute (`int f(int) [[tag]];`) relies on
  `exceptionSpecification()` being reached on that path; `int [[tag]] b;` relies on
  `declarator()`'s leading `skipAttributes`.
- F16: `int x{5};` / `int x = {5};` on a scalar member recorded as the member initialiser;
  `memberInitialiser` (ParserClass.cpp) reads `{v}`, `checkNarrowing`s it and expects `}`;
  `{}`/`{0}` still memset; a list on a class, array or reference stays refused (message reworded,
  one site). Cases `member-brace-initialiser.cpp` (needs `.expected`),
  `member-brace-narrowing-refused.*`. `braced-member-init-refused.*` deleted.
- F33: `externTemplateDeclaration()` (ParserConst.cpp) reads `extern template ...;` to its `;`
  and drops it, called from `topLevel` before `templateDeclaration()`; the refusal in
  `unqualifiedSpecifiers` is gone. Two-file case `extern-template.cpp` + `.part.cpp` (needs
  `.expected`; will need an `extern-template.nonames` on all three targets - clang emits none
  of the suppressed specializations in the declaring unit, cpp11 emits them all).
  `extern-template-refused.*` deleted. `docs/CONFORMANCE.md` gained the section
  "`extern template` suppresses nothing".
- `docs/EXCLUSIONS.md`: alias entry removed; attribute, braced-member and explicit-instantiation
  entries rewritten; every ParserType.cpp citation remapped by message (main's inventory against
  the branch's). `tools/exclusions --check` on the Mac: **127 sites, 0 uncited, 0 stale**.
  `tools/comment-lines` on the Mac: **0 over the cap**. `src/parser/ParserType.cpp` kept CRLF
  (2488 lines, 2488 CRLF).

**Still to do for the four**: build (`msvc\build.cmd` in `C:\cxx1\rtsdiv\ws-c2`), fix whatever
does not compile, generate the four `.expected` files with VS clang and confirm the five `.error`
columns, write `extern-template.nonames`, compare the two goldens (recorded from `0e64e73`,
nothing compared yet), run.sh -O0, names.sh + overload.sh, exclusions --check and make comments
on the box, seal check; then split this WIP into one commit per item.

**Untouched, left for the next session**: F08 (inline namespace), F21 (anonymous union), F15
(design note), the F19 neighbour check, D12's attributes half beyond F10 itself, and the extra
item (destructor access for a local, a temporary, and the members/bases an implicit destructor
destroys). The gates run on the Linux box are skipped (not permitted); run-cases.cmd and
tms6747.sh at -O2 skipped: budget.

**Running on the Windows box: nothing** (unreachable). Linux box not used.

## Earlier stop, 2026-10-08

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
