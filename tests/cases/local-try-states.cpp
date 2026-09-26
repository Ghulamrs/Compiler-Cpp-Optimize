// A destructible local and a `try` in one function, in every shape the FH3
// state tree has to describe - and the same shapes on the Itanium targets,
// where two of them leaked until this case was written. The Microsoft tables
// are one tree now: a region's state has the state in force where it opened
// as its toState, a cleanup's state outlives its stretch until the block that
// built the object ends (`stateend`), and a try's `tryLow..tryHigh` spans
// every state its body opened. clang's output, under the ledger; on
// x86_64-windows the Windows box runs it.
#include "lifetime.h"
extern "C" int printf(const char *, ...);
struct S { int v; S(int n); ~S(); };
S::S(int n) : v(n) { printf("+%d ", v); lfBuilt(this, "S"); }
S::~S() { lfGone(this, "S"); printf("-%d ", v); }
void risky(int k) { if (k) throw k; }
int a1(int k) {                       // a local beside a try
    S a(1);
    try { risky(k); printf("body "); } catch (int e) { printf("caught%d ", e); }
    return 0;
}
int b1(int k) {                       // a local inside the try body
    try { S b(2); risky(k); printf("body "); } catch (int e) { printf("caught%d ", e); }
    return 0;
}
int c1(int k) {                       // two sibling blocks, then a throw outside both
    { S c(3); printf("c "); }
    { S d(4); printf("d "); }
    risky(k);
    return 0;
}
int d1(int k) {                       // a try inside a try body, a local in each
    S o(5);
    try {
        S p(6);
        try { S q(7); risky(k); printf("inner "); }
        catch (double) { printf("no "); }
        printf("after-inner ");
    } catch (int e) { printf("outer%d ", e); }
    return 0;
}
int e1(int k) {                       // a throw out of the inner handler, the outer body's local alive
    S o(11);
    try {
        S p(12);
        try { risky(k); printf("inner "); }
        catch (int e) { printf("h%d ", e); if (e == 9) throw 2.5; }
        printf("after ");
    } catch (double) { printf("outer "); }
    return 0;
}
int f1(int k) {                       // a loop body holding a local and a try, left by break
    int n = 0;
    for (int i = 0; i < 3; i++) {
        S s(20 + i);
        try { risky(k == i ? 1 : 0); n++; } catch (int) { printf("c%d ", i); break; }
    }
    return n;
}
int g1(int k) {                       // a local built after the try, a return from inside it
    S a(30);
    try { if (k) return 7; } catch (int) { }
    S b(31);
    risky(k);
    return 0;
}
int h1(int k) {                       // a temporary in the try body, then a local
    try { int z = S(40).v; (void)z; S t(41); risky(k); printf("t "); }
    catch (int e) { printf("h%d ", e); }
    return 0;
}
int i1(int k) {                       // a return out of the handler past a local outside the try
    S a(50);
    try { risky(k); } catch (int e) { printf("r%d ", e); return e; }
    printf("fell ");
    return 0;
}
int main() {
    lfWatch();
    a1(0); a1(9); printf("\n");
    b1(0); b1(9); printf("\n");
    try { c1(0); c1(9); } catch (int e) { printf("main%d ", e); } printf("\n");
    d1(0); d1(9); printf("\n");
    e1(0); e1(9); printf("\n");
    printf("%d ", f1(5)); printf("%d ", f1(1)); printf("\n");
    g1(0); g1(1); try { g1(2); } catch (int e) { printf("m%d ", e); } printf("\n");
    h1(0); h1(9); printf("\n");
    printf("%d ", i1(0)); printf("%d ", i1(9)); printf("\n");
    return 0;
}
