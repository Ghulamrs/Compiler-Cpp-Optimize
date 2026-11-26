#include <cstdio>
int attempt = 0; int init() { if (++attempt < 2) throw 1; return 42; }
int get() { static int v = init(); return v; }
int main() { try { get(); } catch (int) { std::printf("caught "); } std::printf("%d %d %d\n", get(), get(), attempt); return 0; }
