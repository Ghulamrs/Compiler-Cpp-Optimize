// A static local whose initialiser throws is retried on the next call ([stmt.dcl]/4); a static local initialised by a function call that itself calls the enclosing function sees the partial state (deadlock-free single thread: recursive init is UB, so test only the retry).
#include <cstdio>
int attempts = 0;
int mayThrow() { if (++attempts < 3) throw attempts; return 42; }
int get() { static int v = mayThrow(); return v; }
int main() {
    for (int i = 0; i < 4; ++i) { try { std::printf("%d ", get()); } catch (int e) { std::printf("throw%d ", e); } }
    std::printf("%d\n", attempts);
    return 0;
}
