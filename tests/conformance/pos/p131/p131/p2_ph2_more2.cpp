#include <cstdio>
struct P { int a, b; P(int x, int y) : a(x), b(y) {} };
struct B { int k; B(int v) : k(v) {} };
struct S : B { int i; double d; P p; char c; int z; S(int v) : B{v * 2}, i{v}, d{2.5}, p{v, v + 1}, c{'q'}, z{} {} S() : S{7} {} };
struct Q { int *p; long l; Q(int *q) : p{q}, l{3L} {} };
struct M { int a; int b; }; int M::*pm = &M::b; void (S::*pf)() = 0;
int main() { S s; S t(1); int x = 9; Q q(&x); const char *cp = "z"; bool e = cp; std::printf("%d %d %g %d %d %c %d %d %d %d %d ", s.k, s.i, s.d, s.p.a, s.p.b, s.c, s.z, t.k, *q.p, (int)q.l, (int)e); if (pm && !pf) std::printf("ok"); std::printf("\n"); return 0; }
