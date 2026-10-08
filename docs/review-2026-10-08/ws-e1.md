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

## Gates (Windows PC only)

- Windows cases, GNU spelling: -O0 610/1 at bdda175, the one being the stale
  template-explicit-function.error, now deleted; -O2 run of bdda175 still in flight when stopped
  (the previous full -O2 run, before F31/F07b, was 386/0).
- names: every new case agrees with clang on all three targets or carries a measured `.nonames`;
  the full names.sh/overload.sh run was not done.
- emit golden: recorded at 2955c0b in C:\cxx1\rtsdiv\ws-e1\tests\out-emit.golden; the diff was
  not run - not read.
- Not run: Compiler++ build, examples/ build, tms6747.sh, `make comments` on the box (Mac
  `tools/comment-lines`: 0), exclusions --check is not 0/0 (the document was stale before; the
  ParserTemplate.cpp citations are now all live).
- Linux box: all gates skipped on the user's instruction (g++ build, run.sh -O0/-O2,
  tms6747.sh). A clone was made at ~/ride-5.1/ws-e1 (2dd633b) before the instruction; nothing
  built or run there.

## Stopped here

Done: all five items implemented with cases. Half done: the gate. Untouched: emit golden diff,
names.sh/overload.sh full runs, Compiler++ and examples builds, tms6747.sh, MASM spelling.
Still running on the Windows box when stopped: `run-cases.cmd` at -O2 in C:\cxx1\rtsdiv\ws-e1
(writes winout\, nothing else). C:\cxx1\rtsdiv\ws-e1-base is a worktree at 2955c0b with its own
cxx1-msvc.exe, for base comparisons.

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
