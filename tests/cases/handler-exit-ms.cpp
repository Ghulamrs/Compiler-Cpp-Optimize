// `return`, `break` and `continue` out of a Microsoft handler. A handler on
// x86_64-windows is a funclet - a function of its own the runtime calls with
// the parent's frame under rbp - so it cannot jump into the parent: it hands
// back the address to carry on at, in rax, and the runtime resumes there. An
// early exit therefore leaves the funclet through its own epilogue with a
// label the parent lays down after the `try` (FuncletLeave), where the parent
// does what the handler asked: returns the value the handler saved in the
// parent's frame, breaks the loop the try sits in, or continues it. A caught
// object by value is destroyed on that way out as on every other. The Itanium
// targets take the ordinary path through __cxa_end_catch; the answers are the
// same on all four. clang's output; the Windows box runs it.
extern "C" int printf(const char *, ...);
static int live = 0;
struct E { int v; E(int x); E(const E &o); ~E(); };
E::E(int x) : v(x) { live++; }
E::E(const E &o) : v(o.v) { live++; }
E::~E() { live--; }
struct Big { int a, b, c, d, e; };

static void boom(int x) { throw x; }
static void boomE(int x) { throw E(x); }

static int retInt(int x) { try { boom(x); } catch (int e) { return e * 10; } return -1; }
static int retVal(int x) { try { boomE(x); } catch (E e) { return e.v + 1; } return -1; }
static void retVoid(int *out) { try { boom(3); } catch (int e) { *out = e; return; } *out = -1; }
static Big retBig() { Big b = { 1, 2, 3, 4, 5 }; try { boom(9); } catch (int e) { b.e = e; return b; } b.a = -1; return b; }
static double retDouble() { try { boom(2); } catch (int e) { return e * 1.5; } return -1.0; }
static int loopBreak() {
    int n = 0;
    for (int i = 0; i < 10; i++) {
        try { if (i == 4) boom(i); n++; } catch (int) { break; }
    }
    return n;
}
static int loopContinue() {
    int n = 0;
    for (int i = 0; i < 6; i++) {
        try { if (i % 2) boom(i); } catch (int) { continue; }
        n += 100;
    }
    return n;
}
static int twoTries(int k) {
    try { boom(k); } catch (int e) { if (e == 1) return 11; }
    try { boom(k); } catch (int e) { if (e == 2) return 22; }
    return 33;
}
static int noThrow() { try { } catch (int) { return 5; } return 6; }
int main() {
    int v = 0;
    retVoid(&v);
    Big b = retBig();
    printf("%d %d %d %d %g\n", retInt(7), retVal(4), v, b.e, retDouble());
    printf("%d %d %d %d %d %d\n", loopBreak(), loopContinue(), twoTries(1), twoTries(2), twoTries(3), noThrow());
    printf("live %d\n", live);
    return 0;
}
