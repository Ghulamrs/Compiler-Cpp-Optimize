# WS-E1 - templates (review 2026-10-08): handover

Branch `review/e1-templates`, from 2955c0b. Commits: bdda175 (all five items), and the
commit carrying this note (EXCLUSIONS citations for ParserTemplate.cpp, the stale
`template-explicit-function` refusal case deleted).

## Items

| item | state | cases |
| --- | --- | --- |
| F23 forwarding references, collapsing, std::forward/move | landed | forwarding-reference, reference-collapsing, auto-forwarding-reference, forwarding-constructor, forwarding-pack, range-for-rvalue-reference-refused |
| F30 default template args on the deduced path | landed | function-template-default-argument |
| F34 out-of-line ctor/dtor of a class template | landed | template-out-of-line-constructor (template-out-of-line-ctor.error deleted) |
| F31 explicit specialization of a function template | landed | function-template-explicit-specialization (template-explicit-function.error deleted) |
| F07b alias templates | landed | alias-template (alias-template-refused deleted) |
| V3 | partly: the x86_64-linux C2-only `.nonames` cannot go - clang emits only C2 for a template specialization's constructor written out of line too (measured); temporary-member-call.nonames reworded |

What F23 pulled in beyond the row: `auto &&` deduces as a forwarding reference (it was always
`U &`, so `auto &&b = 5` was refused - pre-existing, base compiler measured); a pack of
forwarding references `A &&... a` / `const A &... a`; a pattern expanded with `...`
(`f(std::forward<A>(a)...)`, tokens copied per member, as a lambda's body is); a member
template's pack bound at instantiation (it was always empty); a pack reference parameter's
slot typed as a pointer (it stored 4 bytes - crash); a constructor template at U = the class's
own arguments keyed once (was "ambiguous with itself", pre-existing).

New named refusals (EXCLUSIONS updated): template arguments partly written and the rest left
to deduction (`make<P>(a, b)`); explicit specialization of a member function template; an
explicit specialization declared without its definition. A `T &&` range-for variable is now
the ill-formed-binding message, not "not supported yet".

## Edits outside WS-E1's files (for the merge after C1/D/E2)

- `ParserType.cpp` `declarator`: the two lines building `&&` and `&` collapse a bound reference
  ([dcl.ref]/6). Cannot live in ParserTemplate.cpp: the declarator is where `T &&` is built.
- `ParserOverload.cpp` `resolveOperator` (2 lines) and `resolveOverload` member-template branch
  (1 line): argument types for deduction via `deductionArgType` (lvalue -> `U &`).
- `ParserStmt.cpp`: `atParenInitialiser` knows a pack declarator (`A &&... a` as first parameter);
  range-for `auto &&` deduces from an lvalue and the explicit `T &&` refusal reworded; the
  condition declaration deduces via `deductionArgType`.
- `ParserClass.cpp` `packParameter`: reads `[const] Ts [&|&&]... name` through `packDeclarator`.
- `ParserTopLevel.cpp`: a pack reference parameter's slot is a pointer; a member template
  specialization whose key equals its constructor key is indexed once.
- `ParserExprCall.cpp` `parseArguments`: one call to `expandPackPattern`.

## Gates

Scope on 2026-10-09 (user, via the coordinator): the weekly budget being nearly spent, the
gate is run.sh -O0, the emit golden diff, names.sh + overload.sh, exclusions --check, make
comments, seal check, and the E1 cases themselves compiled and run on the box at -O0 and -O2
beside clang's output. Skipped on that instruction: Compiler++ build, examples/ build,
run-cases.cmd -O2, tms6747.sh -O2, run.sh -O2.

| gate | result |
| --- | --- |
| `tools/exclusions --check docs/EXCLUSIONS.md` | 130 sites, 103 uncited, 99 stale at 335ac4d (base 2955c0b: 131 / 116 / 112). **No E1 file is uncited or stale**: every ParserTemplate.cpp, ParserInternal.h and include/utility refusal is cited and live. The remaining 103/99 are the inherited stale document (WS-A regenerates it); not rewritten here. The one ParserTemplate.cpp line the tool still counted, the self-naming `auto` of [dcl.spec.auto]/3 (base `:1077`), refuses ill-formed C++11 and belongs in neither document - its message said "none yet to use" and the bare-"yet" rule matched it; reworded in 335ac4d, the `.error` case unchanged. |
| `make comments` (`tools/comment-lines`) | 0 groups over the cap |
| `tools/seal check` | seal 1.6: 155 files, 9 differ - include/utility, Parser.h, ParserClass.cpp, ParserExprCall.cpp, ParserOverload.cpp, ParserStmt.cpp, ParserTemplate.cpp, ParserTopLevel.cpp, ParserType.cpp: exactly E1's edits (the three owned files and the six outside edits listed above). Expected until the 1.7 reseal. |
| `tests/run.sh` -O0 on the box | **blocked**: Windows box unreachable (see below) |
| emit golden diff against 2955c0b | **blocked**: the golden is on the box |
| `tests/names.sh`, `tests/overload.sh` on the box | **blocked**; at bdda175 every new case agreed with clang on all three targets or carries a measured `.nonames` |
| E1's cases run on the box, -O0 and -O2, beside clang | **blocked**; every mended item has a case already (table above), none had to be written |
| run-cases.cmd -O0 | 610/1 at bdda175, the one being the since-deleted template-explicit-function.error; not re-run at 335ac4d |
| run-cases.cmd -O2, tms6747.sh -O0/-O2, Compiler++ build, examples/ build, run.sh -O2 | skipped: budget |
| MASM spelling | waits for F |
| Linux box (g++ build, run.sh, tms6747.sh) | skipped on the user's instruction |

**The Windows box was down for the whole of the 2026-10-09 session.** `ssh windows` and
`ping 192.168.100.84` from 06:55 to 07:20: "Operation timed out", then "Host is down"; the ARP
entry incomplete on en0; the hostname does not resolve. Past the plan's ~20-minute block, so
recorded and the box-bound gates left as blocked rather than run anywhere else (the Mac does
not build or test, the Linux box is excluded). To finish the gate when the box is back, in
C:\cxx1\rtsdiv\ws-e1 (`git fetch origin review/e1-templates && git checkout 335ac4d`, then
`msvc\build.cmd`): `tasklist` first for the stray `run-cases.cmd -O2` of 2026-10-08;
`tests/run.sh` -O0 under Git bash; `tests/emit.sh` and read every file the golden at 2955c0b
reports changed (the two F23 cases forwarding-* and the `std::forward`/`move` in `<utility>`
are the expected movers - nothing existing calls them; any other change is to be read);
names.sh and overload.sh under Git bash with VS LLVM first on PATH; and the ten cases above
by hand at -O0 and -O2, output beside their `.expected`.

## Stopped here, 2026-10-09

Done: the three source-level gates (exclusions on E1's files, comments, seal) and the one
reword they asked for. Blocked by the box: everything that builds or runs. The
2026-10-08 state - items landed, -O0 cases 610/1 with the one stale refusal since deleted -
stands as the last measurement. Head 335ac4d, pushed to origin/review/e1-templates so the box
can fetch it.

## Draft CLAUDE.md sections (for the main session)

**Forwarding references, 2026-10-08.** [temp.deduct.call]/3: `T &&` (and `auto &&`) given an
lvalue of type U deduces `U &`; an expression's type never carries its category, so
`deductionArgType` hands deduction `U &` for an lvalue and `deduceOne` binds it when the
pattern is a bare `T &&`. [dcl.ref]/6 collapsing is in the declarator and in `auto`
substitution. `<utility>`'s `move` and `forward` are the standard's. Names: `_Z3setIRiEvOT_i`
for an lvalue, measured. A pack of them and a pattern expanded with `...` came with it.

**Default template arguments on the deduced path.** A parameter left unbound takes its default,
replayed with the earlier ones bound, under `inTemplateArgs_` so the `>` after `N = 3` ends it.

**Out-of-line constructors of a class template.** From the `Box` after `Box<T>::` the tokens are
shaped as a held constructor, so they are replayed by `replayInlineBodies` with the class's
owner, gated on the constructor (destructor) key being used. clang on x86_64-linux still emits
only C2 for these.

**Function explicit specialization.** The specialization the primary would make - same key,
same name from the primary's pattern - with its own tokens (the `<...>` dropped) as the body;
the most specialized primary it fits is chosen; always emitted.

**Alias templates.** Recorded with the class templates (`isAlias`); `instantiateClass` reads the
type-id with the arguments bound and returns it, so the alias is transparent to deduction and
to both manglers.
