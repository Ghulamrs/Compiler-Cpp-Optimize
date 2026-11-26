#include <cstdio>
struct B { int v = 1; int f() { return 2; } }; struct P : private B { int get() { return v + f(); } }; struct Q : protected B { int get() { return v; } }; struct R : Q { int get2() { return v + 10; } };
int main() { P p; Q q; R r; std::printf("%d %d %d\n", p.get(), q.get(), r.get2()); return 0; }
