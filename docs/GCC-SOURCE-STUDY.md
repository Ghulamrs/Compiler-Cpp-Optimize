# GCC's optimizer, read in its source: what each part is, and what cxx1 takes

Fable 5.1, 2026-09-24, session 1 of two on branch `gcc-scheme`. This goes
below `docs/GCC-SCHEME-2026-09-24.md`, which read GCC's manuals and
`passes.def`: here the source itself was read, file by file, for the *design*
of each part. GCC is GPL-3 and cxx1 is not; nothing below is copied, every
class in cxx1 is written from the description here, and the descriptions are
summaries in this document's own words.

Files read, from the GitHub mirror of `gcc/` at master on 2026-09-24:
`passes.def`, `tree-pass.h`, `passes.cc` (`execute_one_pass`,
`execute_function_todo`, `execute_todo`, `update_properties_after_pass`,
`verify_curr_properties`, `pass_init_dump_file`, `execute_function_dump`),
`basic-block.h`, `cfg.h`, `cfg-flags.def`, `cfghooks.h`, `dominance.h`,
`dominance.cc` (the algorithm comment and the interface), `df.h`,
`df-core.cc` (the overview, `df_analyze`, `df_analyze_problem`,
`df_worklist_dataflow`), `df-problems.cc` (the LR problem in full, the RD,
LIVE and CHAIN entry points), `web.cc` (whole), `cse.cc`, `fwprop.cc`,
`cprop.cc`, `combine.cc`, `dce.cc`, `dse.cc` (the head comments and leading
definitions), `ira.cc` and `lra.cc` (the design comments), `ira-int.h`
(`ira_allocno`), `ipa-inline.cc` (the design comment, `caller_growth_limits`,
`want_inline_small_function_p`, `edge_badness`, the start of
`inline_small_functions`), `ipa-inline.h`, `ipa-fnsummary.h`, `params.opt`
(the inline parameters), `opts.cc` (`default_options_table`), `recog.h`
(`recog_data_d`, `insn_operand_data`, `insn_data_d`), `regs.h`, `rtl.h`.

---

## 1. The pass manager: `tree-pass.h`, `passes.cc`, `passes.def`

**What a pass is.** `pass_data` is the constant description: a type (GIMPLE,
RTL, IPA), a short name that is also the dump file's suffix (a leading `*`
means no dump), a timer id, three property masks (`properties_required`,
`properties_provided`, `properties_destroyed`) and two TODO masks
(`todo_flags_start`, `todo_flags_finish`). `opt_pass` adds behaviour: a
virtual `gate(function *)`, a virtual `execute(function *)` returning further
TODO flags, `clone()` for a pass that appears more than once in the pipeline,
and two links - `sub`, the list of sub-passes run only if the gate passes, and
`next`, the next pass regardless. So the pipeline is a tree: a pass with
`sub` is a group whose gate guards the whole group.

**Properties.** `PROP_cfg`, `PROP_ssa`, `PROP_rtl`, `PROP_loops`,
`PROP_cfglayout` and so on are bits on `function::curr_properties`. Before a
pass runs, checking builds assert `(curr_properties & required) == required`;
after it, `curr_properties = (curr_properties | provided) & ~destroyed`. That
is the whole mechanism - one line each way - and it is what makes a stale
CFG a caught error instead of a miscompile.

**TODO flags.** Work the manager does around a pass rather than the pass
itself: `TODO_cleanup_cfg`, the `TODO_update_ssa*` family,
`TODO_remove_unused_locals`, `TODO_rebuild_cgraph_edges`, `TODO_df_finish`
(drop the optional dataflow problems - after dumping, so the dump can show
them), `TODO_df_verify`, `TODO_verify_il`. Start flags run before `execute`,
finish flags and the flags `execute` returned run after.

**`execute_one_pass`.** In order: publish a before-pass event; check the
gate (a plugin may override it); open the dump file and print the function
header; start the timer; run the start TODOs; verify required properties;
`execute`; update properties; run the finish TODOs plus verification; stop
the timer; dump the function to the pass's dump file; close it; collect
garbage. `execute_pass_list` walks `next`, and for a pass whose gate passed
and that has `sub`, recurses into the sub-list.

**Dumps.** Every pass with a name gets a numbered dump file
(`-fdump-rtl-<name>`, `-fdump-tree-<name>`); the dump is taken *after* the
pass, of the whole function, with the CFG if asked. `-fdump-*-all` opens all
of them.

**`passes.def`.** The pipeline is one declarative file processed by an awk
script into `pass-instances.def`: `NEXT_PASS (name)` in order, with
`PUSH_INSERT_PASSES_WITHIN (group)` / `POP_INSERT_PASSES ()` for the
sub-lists, and a pass may be listed several times (each instance numbered).
The four lists are the lowering passes, the small IPA passes, the regular IPA
passes and `all_passes` (everything per function after IPA, ending in
`pass_expand` and `pass_rest_of_compilation`, the RTL pipeline). The RTL
order for what matters to cxx1: `jump`, `cse`, `fwprop`, `cprop`, `pre`,
`hoist`, `cprop`, `store_motion`, `cse_after_global_opts`, `ifcvt`, `loop2`
(invariants, unrolling), `web`, `cprop`, `cse2`, `dse1`, `fwprop_addr`,
`ud_rtl_dce`, `ext_dce`, `combine`, `late_combine`, `if_after_combine`,
`jump_after_combine`, `split_all_insns`, `sched1`, `ira`, `reload` (LRA),
then `postreload_cse`, `late_combine`, `gcse2`, `ree`, `compare_elim`,
`thread_prologue_and_epilogue`, `dse2`, `stack_adjustments`, `jump2`,
`peephole2`, `if_after_reload`, `regrename`, `fold_mem_offsets`,
`cprop_hardreg`, `fast_rtl_dce`, `reorder_blocks`, `sched2`, and finally
`compute_alignments`, `shorten_branches`, `dwarf2_frame`, `final`.

**Taken into cxx1.** `opt::Pass` with a `PassInfo` (name, required /
provided / destroyed properties, start TODOs), `gate()` and `execute()`
returning whether it changed anything; `opt::Group` as the pass with a
sub-list, extended with the repeat policy cxx1's driver already had ("run
the group until a round changes nothing, or a limit"); `opt::Function::props`
with the same one-line update; the pipeline declared once in
`OptPipeline.cpp`; a dump after any named pass through `CXX1_DUMP_MIR`. The
properties cxx1 needs today are two: the flow graph describes the stream, and
every register is physical. Left out: pass types (cxx1 has one IR level),
timers, plugins, cloning (a pass listed twice is simply constructed twice).

## 2. The function and its CFG: `basic-block.h`, `cfg.h`, `cfg-flags.def`, `cfghooks.h`

**`basic_block_def`.** Two edge vectors, `preds` and `succs`; a pass-private
`aux`; the innermost loop (`loop_father`); the dominator-tree nodes for both
directions (`dom[2]`); the chain links `prev_bb`/`next_bb`; the IR-specific
part (first and last insn for RTL; statement and PHI sequences for GIMPLE);
flags; an index; an execution count. Blocks need not start with a label nor
end with a jump. Two fixed blocks exist in every function: ENTRY (0) and
EXIT (1), holding no instructions.

**`edge_def`.** `src`, `dest`, instructions queued on the edge (for
insertions on critical edges), `aux`, a source location for an implicit
goto, the index in `dest->preds`, flags and a probability. The flags that
matter: `FALLTHRU`, `ABNORMAL`, `ABNORMAL_CALL`, `EH` (a possible transfer
from a trapping instruction to its handler), `PRESERVE` (never merge the
blocks across this edge - used for the post-landing-pad block), `FAKE` (an
edge to EXIT from a block that never exits, so the reverse CFG can be
walked), `DFS_BACK`, `SIBCALL`, `LOOP_EXIT`, `CROSSING`. `EDGE_COMPLEX` is
the union of the abnormal kinds: an edge no pass may split.

**`control_flow_graph`.** Entry and exit pointers, the block table by index,
counts, a label-to-block map, the state of the dominators per direction
(`DOM_NONE`, `DOM_NO_FAST_QUERY`, `DOM_OK`), and allocators for temporary
flag bits. `cfghooks.h` is the IR-independent interface (split block, merge
blocks, redirect edge, delete block, and so on) implemented once for GIMPLE
and once for RTL.

**Taken into cxx1.** `opt::Edge {from, to, kind}` with kinds Fallthrough,
Jump, Return, Leave (cxx1's "somewhere this function cannot see": all live)
and Eh (declared, none made yet - the landing pad remains a block with no
predecessors, which is safe only because no pass runs in a function with
one, exactly as the study's gap table says). `opt::Block` keeps `succs` and
`preds` as edge indices, `begin`/`end` into the stream, the liveness
solution, and `idom`. The exit is a target (`Edge::kExit`), not a block, since
nothing in cxx1 walks the reverse graph yet. Left out: `aux`, probabilities,
the loop pointer (loop depth is still counted by `promoteLocals`'
back-jump scan), the hooks layer (one IR).

## 3. Dominators: `dominance.h`, `dominance.cc`

Lengauer-Tarjan with path compression, computing each block's immediate
dominator in either direction (`CDI_DOMINATORS`, `CDI_POST_DOMINATORS`);
`calculate_dominance_info`, `free_dominance_info`, `get_immediate_dominator`,
`dominated_by_p` (constant time through DFS in/out numbers once
`DOM_OK`), `nearest_common_dominator`, and incremental repair
(`iterate_fix_dominators`, `recompute_dominator`) for passes that edit the
CFG. The state is per function and per direction, and the pass manager
asserts that no pass leaves post-dominators computed.

**Taken into cxx1.** `FlowOf::dominators()` - the simpler iterative
algorithm over a reverse postorder (Cooper, Harvey, Kennedy), which is what a
graph of cxx1's size wants - stores `Block::idom`, `dominates(a, b)` walks
the tree, and a `dominated` flag is cleared by every rebuild. Forward only;
no incremental repair (the CFG is rebuilt, not edited, between passes).

## 4. The dataflow framework: `df.h`, `df-core.cc`, `df-problems.cc`

**Three layers.** *Scanning*: for every insn a `df_insn_info` with three
ref lists - defs, uses and eq-uses (uses inside REG_EQUAL notes); each
`df_ref` names a register, its insn, flags (partial, read-write, conditional,
may-clobber, ...) and a `chain` head for def-use or use-def links. Artificial
refs hang off blocks instead of insns: uses at the end of every block for
registers always live (the stack pointer), defs at the top of a block that is
an EH landing pad (the registers the unwinder sets), defs at the end of the
entry block (registers live on entry). *Local*: per block, a transfer
function (for LR the `use`/`def` sets, computed by a backward walk of the
block's refs; a partial def counts as a use too). *Global*: an iterative
worklist solver over a postorder.

**A problem.** `df_problem` is a table of function pointers: allocate,
reset, free per block, local compute, init, the dataflow function, two
confluence operators (for blocks with no edges, and per edge), the transfer
function, finalize, free, dump (start, top of block, bottom of block, before
and after an insn), verify, plus `dependent_problem` (chains depend on RD)
and the size of the per-block record. `dataflow` is one instance: the
problem, its per-block info array, `solutions_dirty`, `computed`,
`optional_p`. The ids: SCAN, LR (live registers, backward), LR_DCE, LIVE (LR
intersected with UR - reachable from a def), RD (reaching defs), CHAIN
(def-use / use-def), WORD_LR, NOTE, MD, MIR.

**Usage.** A pass adds the optional problems it needs
(`df_chain_add_problem (DF_UD_CHAIN)`, `df_live_add_problem ()`), sets
flags (`DF_NO_HARD_REGS`, `DF_RD_PRUNE_DEAD_DEFS`, `DF_DEFER_INSN_RESCAN`),
calls `df_analyze ()`, works, and either calls `df_finish_pass` or lets
`TODO_df_finish` do it. `df_analyze` computes a postorder (dropping
unreachable blocks), then solves each problem in dependency order:
allocate, local compute, solve, finalize. LR and LIVE are kept alive across
the whole RTL backend and only their dirty transfer functions are recomputed;
everything else is rebuilt on demand.

**Incremental scanning, four ways.** Immediate rescan as insns change (the
default, wrong when only one of the two chain directions is built); deferred
rescan (batched at the next analyze); total rescan at the end (the
allocator, which changes every insn); do it yourself. The file's own
philosophy: bitmaps are not worth keeping incrementally, chains are, and the
fastest way to build chains is from reaching definitions.

**LR in detail** (`df_lr_bb_local_compute`, `df_lr_transfer_function`,
`df_lr_confluence_n`): `in = use | (out & ~def)`; over an EH edge the
call-clobbered registers are killed before the union (they die across the
throw); the always-live hard registers are added at every confluence.

**Taken into cxx1.** `Flow` is the scanning layer (its `effects` per entry
are `df_insn_info`'s defs and uses, folded into register sets) plus the LR
problem with two extra bits cxx1 needs - `wide` (read above the low four
bytes) and `flags` - and a `live()` that solves only when dirty
(`solutions_dirty`); the pass manager marks it dirty after any pass that
changed something, and every rebuild does. Reaching definitions
(`ReachingDefs`, in `OptDataflow.{h,cpp}`) is the second problem, extracted
from `buildWebs`, which now consumes it instead of computing it inline - the
dependency `web -> RD` as GCC has it. Left out: the function-pointer table
(two problems, two classes), artificial refs (an EH pad's registers will be
an edge-kind rule in the confluence, as GCC's EH kill is), deferred
rescanning (passes recompute an entry's effects as they change it, which is
GCC's "immediate rescan"), eq-notes.

## 5. Webs: `web.cc`

A pass of 430 lines and the file GCC itself calls the example use of the
dataflow module. It asks for use-def chains with hard registers excluded
(`DF_NO_HARD_REGS`) and dead defs pruned, then: numbers every use of a
pseudo; makes a union-find entry per def and per use; for each use, unites
it with every def that reaches it (all definitions reaching a use must land
in one register), unites uses of the same uninitialized register into one
web (correct either way, wasteful otherwise), unites a read-write use with
its def in the same insn, and unites operands the machine description says
must match (`match_dup`) and the two sides of a trivial no-op move; then
walks the insns again, and each web's root gets a register - the original
if it is the first web found in that register, a fresh pseudo otherwise -
and every ref is rewritten. The point is that a pseudo whose live ranges
are independent is split into one pseudo per range, so the allocator can
colour them apart.

**Taken into cxx1.** `mir::buildWebs` already did this on physical
registers (the study called it stage 1); what changes here is only that its
reaching-definitions fixpoint becomes the `ReachingDefs` problem. The "pin an
occurrence, not a web" step (splitting a use that must be in a particular
register off with a copy before uniting) is GCC's `match_dup`/constraint
handling turned around, and is session 2's.

## 6. The scalar RTL passes: `cse.cc`, `fwprop.cc`, `cprop.cc`, `combine.cc`, `dce.cc`, `dse.cc`

**cse.** Per extended basic block, forgetting everything at each label
(a global pass, gcse/pre, exists separately). Two structures: a hash table
of expressions and, for registers, *quantity numbers* - registers that hold
the same value share a quantity; a copy copies the quantity, any other write
allocates a new one; each quantity may carry a known constant. An
expression seen again is replaced by the cheapest equivalent (a register
holding it, or a constant). cxx1's `forwardValues` is this at block scope
with cxx1's `Value::id` as the quantity number; the dominator-tree extension
(GCC's `dominator` pass at GIMPLE) is session 2's value numbering.

**fwprop.** Substitutes a def's source into a use that has a single
reaching def (it uses RTL SSA for that), keeping the result if the target
recognises it - constants into uses, copies propagated, and, most usefully,
address arithmetic folded into memory operands (more aggressively inside a
MEM, judged by `address_cost`). cxx1's `foldOffsets` and `foldLoads` are
this; `fwprop_addr` is the instance run before combine.

**cprop.** Global copy and constant propagation as an available-expression
problem over assignments `dest := src`: a hash table of such sets, one
occurrence per block (the last), and a bitmap dataflow of availability.
Also *implicit sets*: a conditional jump on `x == c` makes `x = c` available
on the taken edge.

**combine.** The classic Arizona combiner: for each insn, the LOG_LINKS
(the most recent assignment to each register it uses, within the block)
name candidate producers; two, three or four linked insns are combined by
substituting the producers' values into the consumer, and the result is
kept if it is a valid pattern for the target. Producers must be single
assignments; a call in the middle stops it. This is what turns a load and
an add into an add with a memory operand, and it is the exact shape of
cxx1's `coalesceCopies` / `foldLoads` with the machine description standing
in for cxx1's opcode table.

**dce.** Mark-and-sweep over insns with a worklist: an insn is needed if it
has side effects (stores, calls that are not const/pure, volatile refs,
control), or defines a register a needed insn uses (through def-use chains
for the `ud` variant, through LR liveness for the `fast` variant that runs
after reload). cxx1's `removeDead` is the fast variant.

**dse.** Three techniques: local per block over a value-numbered address
(cselib), global for addresses that are constant or frame-relative, and
post-reload for spill slots. The global problem is backward set-union with
stores as gens and reads as kills; a frame store is dead if every path to
the exit reaches another store to it before any read, or no read at all.
cxx1's `removeDeadStores` is the frame-relative case for whole functions
without a dataflow (it scans the function for any read overlapping the
slot); the backward problem over the CFG is session 2's, and gets it into
functions cut by funclets once EH edges exist.

## 7. Register allocation: `ira.cc`, `ira-int.h`, `lra.cc`

**IRA** is regional graph colouring (Chaitin-Briggs with Briggs' optimistic
colouring) over a tree of regions - the function, then its natural loops -
with coalescing, live-range splitting and hard-register preference done
during colouring rather than as separate passes. An *allocno* is one
pseudo's life in one region, with: its class, its live ranges as program
points, a hard-register cost vector (a call-clobbered register costs the
save/restore around every call the allocno lives across; a register that is
the other operand of a move is cheaper), conflict costs from unassigned
neighbours, `nrefs`, `freq` (execution frequency, summed), memory cost,
calls crossed, copies (preferences to share a register: moves, two-operand
constraints, an output and a dying input). Colouring pushes allocnos on a
stack in *threads* of copy-connected allocnos, pops and assigns the cheapest
register, spills when none is free or memory is cheaper, then improves by
re-spilling where that lowers total cost. Region borders get move code;
after flattening, spilled allocnos get one more try.

**LRA** replaces reload: it iterates to make every insn satisfy its
constraints, generating reload insns and reload pseudos, spilling, splitting
and inheriting, and rematerializing, with its own compact insn
representation kept in sync with the RTL.

**Taken into cxx1.** Nothing built this session; the allocator is session
2's first speed item. What the reading fixes for its design: allocation over
pseudos after webs; costs = references weighted by loop depth at -O2 and by
encoding bytes at -O1, a pseudo live across a call charged for a
caller-saved register's save/restore (so it prefers callee-saved); copies as
preferences, not a separate coalescing pass; no regions (one function,
loop depth as weight), no LRA (cxx1's instruction forms are already
constraint-satisfying, so a spill is a load or store around the use, chosen
by the opcode table's `kRenamable`).

## 8. The inliner: `ipa-inline.cc`, `ipa-inline.h`, `ipa-fnsummary.h`, `params.opt`, `opts.cc`

**Two passes.** `early_inline` runs per function in reverse postorder of
the call graph, right after into-SSA and before the early cleanups: it
inlines the always-profitable calls (bodies smaller than the call, declared
inline and small) and may grow a function by `early-inlining-insns` (6)
per call when the callee is a leaf. `ipa_inline` runs once with summaries
of every function: (1) small functions in order of *badness* through a
priority queue, while the limits allow; (2) unreachable functions removed;
(3) functions called once and not exported inlined into their caller.

**Summaries.** `ipa_size_summary`: `self_size`, `size` (with inlined
callees), estimated stack size. `ipa_fn_summary`: `min_size`, `time`,
`inlinable`, `single_caller`, `growth`. `ipa_call_summary`: the call
statement's own size and time - the growth of an edge is the callee's size
minus the call's size, cached per edge.

**Limits** (`caller_growth_limits`): the largest function on the inline
chain sets the base; the result may not exceed `large-function-insns` (2700)
and `base * (1 + large-function-growth/100)` (100%); stack growth likewise
(`large-stack-frame` 256, `large-stack-frame-growth` 1000%). Unit-wide:
`inline-unit-growth` (40%) over the initial size (`compute_max_insns`).

**Wanting** (`want_inline_small_function_p`): growth <= `max-inline-insns-size`
always; a declared-inline callee up to `max-inline-insns-single` (70),
another up to `max-inline-insns-auto` (15), both doubled by hints
(`inline-heuristics-hint-percent` 200: a call that becomes direct, a loop
whose count becomes known, a hot callee) and waived for a big speedup
(`inline-min-speedup` 30%); a cold call may not grow the caller at all.

**Badness** (`edge_badness`): growth <= 0 is always first; otherwise
`-(time saved * frequency) / (growth * overall_growth^2 * (caller size +
growth))`, the square on the callee's overall growth strongly preferring
callees that can be inlined everywhere and disappear; a "wrapper" penalty
for a small inline caller that would swallow a large non-inline callee.

**Levels** (`default_options_table`): `-finline-functions-called-once` at
-O1; `-finline-small-functions`, `-finline-functions`, `-fipa-cp`,
`-fipa-sra`, `-fipa-icf`, `-fpartial-inlining` at -O2 and -Os alike; -O3
raises the four parameters (single 200, auto 30, early 14, hint 600). -Os
is `optimize = 2` with `optimize_size = 1`: the same table, with the
`SPEED_ONLY` rows (alignment, `reorder-blocks-algorithm=stc`, the
vectorizers, sched1) left off.

**Taken into cxx1.** For session 2's step 1: a per-callee size summary
(the walker's instruction count of its body, cached), a per-call growth
(callee size minus the call sequence), the caller limit as a percentage of
the caller's own size, the unit limit as a percentage, and the level's
parameters in `Costs` - `forSize` choosing "inline only where growth <= 0"
(GCC's `max-inline-insns-size` rule) and speed choosing the auto/single
limits. No badness queue: cxx1 inlines at the walker, site by site in
walk order, so the decision is per site under a running budget.

## 9. The machine description as seen from the passes: `recog.h`, `rtl.h`, `regs.h`

`recog_data` is what `extract_insn` fills for an insn: operands, their
locations, constraint strings, modes, types (`OP_IN`, `OP_OUT`,
`OP_INOUT`), duplicates (`match_dup`) and the number of alternatives;
`insn_data[]` is the generated table per pattern - name, output template,
operand predicates, constraints, modes, whether an operand is `strict_low`
(a partial write) or `allows_mem`. Every pass that asks "may this operand be
a memory reference", "does this insn write its input", "must these two be
the same register" asks these tables. `regs.h` holds the per-register
statistics (`REG_N_REFS`, `REG_FREQ`, `REG_LIVE_LENGTH`) the allocator
weighs.

**Taken into cxx1.** `opt::Opcode` in `OptTable.{h,cpp}`: a kind (the
effects branch), flags (explicit-only, writes-only, immediate source
allowed, renamable frame operand, shift, zero idiom, merges xmm lanes,
leaves flags) and a suffix width - `insn_data` and `recog_data` for an
instruction set of fixed spellings. Modes are the operands' widths already.

---

## 10. The mapping, GCC to cxx1

| GCC | cxx1 (this branch) | Status |
|---|---|---|
| `pass_data` + `opt_pass` (name, gate, execute, properties, TODOs) | `opt::PassInfo` + `opt::Pass` (`OptPass.h`) | built |
| `opt_pass::sub` (a gated group) | `opt::Group`, with the repeat policy | built |
| `passes.def` | `opt::pipelineFor()` in `OptPipeline.cpp` | built |
| `execute_one_pass` / `execute_pass_list` | `opt::PassManager::run` | built |
| `function::curr_properties`, `PROP_*` | `Function::props`, `opt::Prop` | built (two bits) |
| `TODO_*` | `opt::Todo` (drop unnamed labels, rebuild flow) | built |
| `-fdump-rtl-<pass>` | `CXX1_DUMP_MIR=<pass>|all` | built |
| `struct function` + `control_flow_graph` | `opt::Function` (`OptFunction.h`) + `opt::Flow` | built |
| `basic_block_def` (`preds`, `succs`, `dom`) | `opt::Block` (`succs`, `preds`, `idom`) | built |
| `edge_def` + `EDGE_*` flags | `opt::Edge` + `Edge::Kind` | built; `Eh` declared, not made |
| ENTRY / EXIT blocks | block 0 / `Edge::kExit` | built |
| `calculate_dominance_info`, `dominated_by_p` | `Flow::dominators()`, `dominates()` | built, unused by any pass yet |
| `df` scanning (`df_insn_info`) | `Flow::effects` | built (existing) |
| `df_problem` LR | `Flow::solve` / `live()` with `solutionsDirty` | built |
| `df_problem` RD | `opt::ReachingDefs` (`OptDataflow.h`) | built, from `buildWebs` |
| `df_problem` CHAIN (du/ud) | - | session 2 (step "def-use chains") |
| `TODO_df_finish`, `solutions_dirty` | `Flow::touch()` from the manager | built |
| `insn_data`, `recog_data`, constraints | `opt::Opcode`, `opcodeOf` (`OptTable.h`) | built |
| `optimize`, `optimize_size`, params | `opt::Costs` (`OptCosts.h`) | built (existing fields) |
| `web` | `mir::buildWebs` | existing, over RD |
| `cse` (block scope) | `forwardValues` | existing |
| `fwprop`, `fwprop_addr`, `fold_mem_offsets` | `foldLoads`, `foldOffsets` | existing |
| `combine` (copy into producer) | `coalesceCopies` | existing |
| `fast_rtl_dce`, `ext_dce` | `removeDead` | existing |
| `jump` (unreachable, jump-to-next) | `removeUnreachable`, `dropUnnamedLabels` | existing |
| `dse` (frame, global) | `removeDeadStores` | existing, whole functions |
| `ira` + `lra` | `promoteLocals` (a special case) | session 2 |
| `thread_prologue_and_epilogue` | the prologue event rewritten (`finishFrame`) | existing |
| `peephole2` | `shrink` | existing |
| `ipa_inline`, `ipa_fn_summary` | the walker's `inlineTarget`/`small()` | session 2 (step 1) |
| `compute_alignments` | none | decided against, measured |
| `sched1`/`sched2` | none | decided against |
