# The optimizer's IR

cxx1 1.5's -O1 and -O2 work on the assembly the walker has already written. They
clean up after a stack machine: pushes paired with pops, reloads forwarded, a few
locals moved into callee-saved registers. That approach has a ceiling. On the
three-box run of 2026-09-23, Compiler++ at cxx1 -O2 was still 2.3x slower than cl
and g++. The next step is the one GCC takes: optimize an intermediate
representation with pseudo registers, then allocate real registers once.

This document records what was learned from studying GCC, what cxx1 builds,
and the order it is built in.

## What GCC does, and what cxx1 learned from it

Read from GCC 14.2's `passes.def`, `opts.cc` and the Internals manual. The code was
read for its design only and nothing of it is copied: GCC is GPL-3 and cxx1 is
not.

- **Two IRs.** GIMPLE is a target-neutral three-address form in SSA, where most
  scalar optimization happens. RTL is target-shaped: instruction patterns over
  pseudo registers, where combine, register allocation and the late cleanups run.
- **-O1 is a cheap core.** Constant, copy and forward propagation (`ccp`,
  `copy_prop`, `forwprop`), full-redundancy value numbering (`fre`), dead code and
  dead stores (`dce`, `dse`), scalar replacement (`sra`), sinking, the register
  allocator, and combining stack adjustments.
- **-O2 is -O1 and more.** Partial redundancy (`pre`, `gcse`), value ranges
  (`vrp`), CSE across jumps, inlining of small functions, interprocedural
  constant propagation, `peephole2`, scheduling, crossjumping, store merging.
  Alignment is "speed only", the one axis where -O2 and -Os part.
- **Register allocation comes last and is global** (IRA then LRA). Every pseudo in
  the function competes for every register, with costs; spilling is the
  allocator's decision, not the code generator's.

## Where cxx1's IR sits: GCC's RTL, below the walker

cxx1's knowledge of each ABI lives in its walkers: calls, aggregates by value,
exceptions, varargs, x87, three targets, 264 cases green on each. GCC keeps the
same knowledge in its RTL expander and machine descriptions. Rewriting the
walkers to emit a neutral GIMPLE would throw that away. So cxx1's IR is RTL-shaped
and sits where GCC's RTL sits right after expansion:

1. **Read.** The walker's instructions for one function become MIR: the target's
   own opcodes, with every register a *pseudo*. That includes the stack
   machine's pushes and pops, and scalar locals whose address never escapes
   (GCC's SRA and into-SSA, done at this level). Hard registers remain only where
   the target demands them (arguments, returns, `idiv`'s rdx:rax, a shift's
   `%cl`) as short copies.
2. **SSA,** over the control-flow graph and its dominators.
3. **Scalar passes,** each a pass over SSA: CCP, copy propagation, value
   numbering over the dominator tree (FRE), DCE, DSE on frame slots, and folding
   addresses and immediates into operands (combine's work at this level).
4. **Out of SSA,** copies on edges, then coalesced.
5. **Register allocation,** graph colouring with coalescing (Chaitin-Briggs).
   Costs are weighted by loop depth; a pseudo live across a call prefers a
   callee-saved register; spills go to frame slots.
6. **Emit** through the target's spelling. The prologue saves the callee-saved
   registers used, with CFI or SEH as each format requires.

Each target brings a reader and a description: operands, effects and constraints.
x86 has these already (`OptIr`, `OptEffects`); arm64 gets its own. The passes
belong to neither target.

## The levels

cxx1's levels mean what cl's do: -O1 is small code, -O2 is fast code. Neither
is given up for the other.

| | -O1 (size) | -O2 (speed) |
|---|---|---|
| propagation, FRE, DCE, DSE | yes | yes |
| register allocation | size costs | speed costs, loop-weighted |
| inlining | only where the body is smaller than the call | small functions (the walker's) |
| PRE, loop-invariant motion | no | yes |
| loop alignment | no | yes |

## Stages, each with its gate

The current optimizer remains -O1 and -O2 until a stage beats it. A stage ships
only when the three boxes are green (Compiler++'s suites, its 258 cases against
cl /O2, cxx1's 264 cases) and neither level's size nor speed got worse.

1. **MIR, reader and writer.** A function round-trips through MIR unchanged.
   Gate: every function of Compiler++ reproduces its assembly byte for byte.
2. **Webs and the register allocator.** Pseudos for the stack machine and the
   promotable locals, graph colouring, spills. This replaces the push/pop pairing
   and the local promotion.
3. **SSA and the -O1 scalar passes.**
4. **The -O2 passes:** PRE, loop-invariant motion, alignment.
5. **arm64:** its reader and description, the same passes.

## What stage 2 needs first

Fable 5.1 reviewed branch `opt` on 2026-09-23, from `02c665b`. It found one
miscompile, now fixed: a file that writes `volatile` is compiled without -O,
because the type system drops the qualifier. It also found four things the
register allocator cannot be built without:

1. **Throwing calls need an edge to their landing pad.** Today a landing pad is a
   block with no predecessors, which is safe only because no pass runs in a
   function that has one. Most real C++ functions have one. Each call inside a try
   range gets its pad as a second successor, and callee-saved webs are pinned
   across it.
2. **Pin an occurrence, not a web.** One `cqo`, `idiv`, `ret` or argument use of
   rax pins the whole web it belongs to. On one case 5,142 of 6,412 webs were
   pinned. The pinned occurrence is to be split off with a copy before webs are
   joined, and that pseudo count is the measure of it. *Done on gcc-scheme
   (`mir::Webs::splitPinned`): over Compiler++ at -O2, 33,395 pseudos became
   55,826, byte-identical output.*
3. **One opcode table.** About a dozen mnemonic lists (`isMove`, `isRmw`,
   `takesImmediate`, `renamable`, `suffixWidth` and more) become one table with
   flags, so every constraint the allocator honours lives in one place.
4. **The inliner needs a cost.** Every small callee is inlined at every site, so
   -O2 is 39% larger than -O1. Each site's slots should be locals that promotion
   can take, calls should say which argument registers they really read, and
   inlining should run on a budget. At -O1, the table above says to inline only
   where the body is smaller than the call.

The funclet question is open too. `settle()` hands a Windows function to the
passes in pieces, and a whole-function allocator cannot see across the cut.
Either allocate before the cut, keeping locals a funclet touches in memory, or
keep today's rule (no allocation in such frames) and say so here.
