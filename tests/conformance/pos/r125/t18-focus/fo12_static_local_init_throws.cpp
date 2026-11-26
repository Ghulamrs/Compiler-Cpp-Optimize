// [stmt.dcl]/4: when a static local's initialiser throws, the initialisation is not complete and is retried on the next entry.
#include <cstdio>
int attempts = 0;
int mayThrow() { if (++attempts < 3) throw attempts; return 42; }
int get() { static int v = mayThrow(); return v; }
int main() {
    for (int i = 0; i < 4; ++i) { try { std::printf("%d ", get()); } catch (int e) { std::printf("throw%d ", e); } }
    std::printf("%d\n", attempts);
    return 0;
}
