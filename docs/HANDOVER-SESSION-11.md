# Handover to session 12: S12 #1, the matmul xmm hoist, priced and not built

Fable 5.1, 2026-09-25, from `93d2f18`. A Linux container: no Mac, no
Windows box, no cl. Read `docs/HANDOVER-SESSION-8.md` for the gate and
`docs/HANDOVER-SESSION-10.md` for S11, whose method this repeats.

## What was priced

The inner loop of `matmul` (`mc[i][j] += x * mb[k][j]`, 4.1M turns at
n=160, five calls) reloads `x` from its frame slot, `-32(%rbp)`, on every
turn. The task was to extend `hoist-invariants` from the GPR palette to
the xmm one so that load moves to the preheader. It was priced first by
hand-editing the compiler's own -O2 assembly for x86_64-linux, assembled
and linked with the system `c++ -no-pie`, 11 interleaved rounds, medians
with the middle-half range, checksums equal:

| build | matmul | what changed |
|---|---|---|
| `93d2f18` -O2 as emitted | 19 (18-20) | |
| the load hoisted into `%xmm3`, `movapd %xmm3, %xmm0` in the loop | 19 (18-20) | one L1 load: **nothing** |
| both, loop heads pinned `.p2align 6` (placement control) | 19 / 19 | still nothing |
| the whole loop cleaned by hand (below) | **13 (13-13)** | 3 `movapd`, 2 `movslq`, 1 load: 32% |
| g++ -O2 | 9 (9-9) | |
| clang -O2 | 4 (4-4) | |

The hoist alone is worth nothing on this machine and is not built. The
loop is throughput-bound with no carried dependency but the counter: a
frame-slot load that hits L1 is one uop of about sixteen, and the store
of `mc[i][j]` it feeds does not wait on it. The 5% floor the task set was
not approached, so no pass was written, nothing was tested and nothing
emitted moved.

## What does pay, and it is not a hoist

The 13 ms line replaced the thirteen-instruction loop body with five:

```
  movslq %eax, %r9
  movsd  (%r10,%r9,8), %xmm0
  mulsd  %xmm3, %xmm0
  addsd  (%r11,%r9,8), %xmm0
  movsd  %xmm0, (%r11,%r9,8)
```

What the emitted loop carries instead: `movsd` into `%xmm0`, `movapd` to
`%xmm2`, the slot reload into `%xmm0`, `movapd %xmm2, %xmm1`, `mulsd`,
`movapd` back to `%xmm2`, the second `movslq %eax, %r9` (the first is
still live), `movsd` into `%xmm0`, another `movapd`, `addsd`, a third
`movslq`, the store. Three of the six removed instructions are register
shuffles the SSE stack discipline leaves behind and the copy-propagation
pass does not fold on xmm; two are the same sign extension recomputed
because value numbering runs within a loop over GPR values only for the
GPR side of the address; one is the operand fold `addsd mem, %xmm` which
the peephole does for GPR arithmetic and not for SSE. That is three
candidates, each on the xmm palette, and the pricing says they are worth
32% of this kernel together where the hoist is worth 0%. Which of the
three carries the 6 ms was not separated - the budget went to the task's
own question - and is the first thing S12 #2 should measure, one edit at
a time, before building any of them.

Nothing else moved: fib, sieve, isort, hash and virtual are within their
middle-half ranges across all four builds.

## What is left

- The xmm copy-propagation, the xmm-side value numbering and the SSE
  memory-operand fold, priced together above and not separated.
- S11's divide-by-constant sign correction, still needing a proof the
  stream cannot give.
- The reference for the next round is still `cxx1-6826941.exe`: this
  session changed no source.
