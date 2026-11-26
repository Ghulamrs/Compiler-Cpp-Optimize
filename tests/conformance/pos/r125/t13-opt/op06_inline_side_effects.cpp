// Inlining must preserve side-effect order, static locals in inlined functions, recursion that inlines partially, functions through pointers, functions with varying argument expressions that have side effects (sequenced ones only).
#include <cstdio>
static int counter = 0;
inline int tick() { return ++counter; }
inline int twiceTick() { int a = tick(); int b = tick(); return a * 10 + b; }
inline int withStatic() { static int s = 0; return ++s; }
int rec(int n) { return n <= 0 ? 0 : n + rec(n - 1); }
inline int viaPtr(int (*f)(int), int v) { return f(v); }
int neg(int x) { return -x; }
inline void noop() {}
inline int sideInArg(int v) { counter += v; return counter; }
int main() {
    std::printf("%d %d\n", twiceTick(), counter);
    int w1 = withStatic(); int w2 = withStatic(); std::printf("%d %d\n", w1, w2);
    std::printf("%d %d\n", rec(100), viaPtr(neg, 5));
    noop(); int s1 = sideInArg(tick()); std::printf("%d %d\n", s1, counter);
    int x = 0; for (int i = 0; i < 5; ++i) x += tick(); std::printf("%d %d\n", x, counter);
    return 0;
}
