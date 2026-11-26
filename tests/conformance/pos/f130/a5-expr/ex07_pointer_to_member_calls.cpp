#include <cstdio>
struct S { int v = 3; int f(int x) const { return v + x; } static int sf(int x) { return x * 2; } virtual int g() { return 1; } };
struct D : S { int g() override { return 2; } };
int main() { S s; D d; int (S::*pf)(int) const = &S::f; int S::*pv = &S::v; int (S::*pg)() = &S::g; int (*ps)(int) = &S::sf; S *p = &d;
std::printf("%d %d %d %d %d %d\n", (s.*pf)(1), (p->*pf)(2), s.*pv, (p->*pg)(), (d.*pg)(), ps(4)); return 0; }
