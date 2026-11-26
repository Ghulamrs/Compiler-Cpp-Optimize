// [except.handle]/15: function-try-blocks on constructors catch member-initialiser exceptions and rethrow implicitly; on ordinary functions act like a try around the body.
#include <cstdio>
struct Mem { Mem(int v) { if (v < 0) throw v; } };
struct C { Mem m; C(int v) : m(v) { std::printf("body "); } };
int f(int v) try { if (v) throw v; return 0; } catch (int e) { return e * 10; }
int main() {
    try { C good(1); C bad(-2); } catch (int e) { std::printf("| rethrown %d\n", e); }
    std::printf("%d %d\n", f(0), f(3));
    return 0;
}
