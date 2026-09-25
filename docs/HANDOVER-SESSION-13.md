# Handover to session 14: S13, the Windows index scaled at pointer width

Fable 5.1, 2026-09-25, from `351a46e`. A Linux container: no Mac, no
Windows box, no cl. Read `docs/HANDOVER-SESSION-8.md` for the gate and
`docs/HANDOVER-SESSION-12.md` for the three xmm gaps this round carries to
x86_64-windows.

## What was wrong, and where

S12 #2 left matmul's inner loop at 6 instructions on x86_64-linux and at
16 on x86_64-windows, each index there being `movslq %esi,%rax; shl $3,
%eax; movslq %eax,%rdx` and every memory operand `(%r8,%rdx,1)`. The
optimizer was never the cause. `Parser::pointerAdd` scaled an offset in
`Kind::Long` - 8 bytes on the two Itanium targets and **4 on Windows** -
so on that target the index was extended to 64 bits, shifted in 32, and
extended again; `foldScale` in `fold-index` takes a 64-bit `shl` only, the
`movslq` no-op rule never met a destination that already held the
extension, and the `addsd mem` fold had no indexed operand to fold.

**The fix is one helper in the parser.** `Parser::ptrdiffType()` answers
`long` where it is 8 bytes and `long long` where it is not; `pointerAdd`
and `pointerSub` compute in it. That is also what `ptrdiff_t` is on the
Microsoft ABI (`__int64`), so a difference of pointers or an index past
2^31 elements is right there now rather than truncated in 32 bits. Neither
optimizer rule was taught a second shape; the Windows loop is Linux's:

    movslq %esi, %rdx
    movsd (%r8,%rdx,8), %xmm2
    movsd 80(%rbp), %xmm0
    mulsd %xmm2, %xmm0
    addsd (%r9,%rdx,8), %xmm0
    movsd %xmm0, (%r9,%rdx,8)

and in MASM `movsxd rdx, esi` ... `addsd xmm0, QWORD PTR [r9+rdx*8]`, which
is the width S12 #2 gave the four SSE arithmetics for exactly this day, and
the first time an emission reaches it - ml64 has to accept it, see below.

## Priced, on this box, by transplanting the Windows shape into the Linux .s

`tools/windows/bench-kernels.cpp`, x86_64-linux -O2, the 6-instruction
loop replaced by the 16-instruction Windows one (r12 as the scratch the
shape needs, saved in the frame), `c++ -no-pie`, 11 interleaved rounds,
medians with the middle-half range, checksums equal:

| loop | matmul | fib | sieve | isort | hash | virtual |
|---|---|---|---|---|---|---|
| Windows shape, 16 | 23 (22-24) | 10 | 72 | 17 | 268 | 141 |
| Linux shape, 6 (unchanged) | 14 (14-16) | 10 | 72 | 18 | 262 | 144 |

39% off matmul, the other five within their ranges. That is the Linux
machine's answer to the Windows shape; the Windows box's own number is
what `bench.cmd` gives.

## Gate

Clean make; `ParserExpr.cpp` clean under `clang++ -std=c++14 -Wall -Wextra
-Werror -pedantic -fsyntax-only -Isrc`; `run.sh` 480/0 at -O0, -O1 and
-O2 (every case run and diffed at each level); `emit.sh` **83 of 839
changed, 12 added, every one of the 83 x86_64-windows** - a parser change
moves -O0, and the diff is the one shape (`movslq; imul %r10d,%eax;
movslq` to `imul %r10,%rax`); `identical.sh LEVELS=0` against
`cxx1-6826941` reports 174 of 582 differ, all of them the Windows -O0
outputs it compares (it compares no Itanium -O0); the Linux bench emission
byte-identical before and after; the MASM spelling at -O1/-O2 over the
cases, and the size proxy at -O2, in the numbers below; `make comments` 63.

Windows -O0 differing against the reference is the change, not a fault:
the reference's `-O0` is the old parser's. The next round's reference is
this commit's `cxx1.exe`, and `identical.sh` at every level against it is
the byte-identity gate again.

## On the Windows box

    tools\windows\bench.cmd -New <this cxx1.exe> -Rounds 11

against the cl /O2 reference, matmul being the kernel that should move
(S12's Linux shape is 15 ms against 18). Then the MASM spelling, since
`addsd xmm0, QWORD PTR [r9+rdx*8]` is emitted for the first time and ml64
has to take it:

    cxx1.exe -O2 -arch x86_64-windows -masm=masm -S tools\windows\bench-kernels.cpp -o bk.asm
    tools\windows\asm-run.cmd bk.asm

and `run-cases.cmd` at -O1 and -O2 through both spellings. If ml64 refuses
the `QWORD PTR` form, the rule to look at is the width table in the MASM
spelling for `addsd subsd mulsd divsd`.

## What is left

- The slot load of `x` (`movsd 80(%rbp), %xmm0`) stays in the loop on both
  targets (S12 #1: hoisting it is worth 0 on Linux).
- S11's divide-by-constant sign correction, still needing a proof.
- `pointerSub`'s result is `long long` on Windows now, which is
  `ptrdiff_t` there; an overload set that told `long` from `long long` on a
  pointer difference would choose differently than before, and rightly.
