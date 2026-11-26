#include <cstdio>
struct S { int a; double d[2]; int f(int) const; int g(); static int st; }; int S::f(int x) const { return a + x; } int S::g() { return 2; } int S::st = 3;
int main() { S s{1, {1.5, 2.5}}; int S::*pa = &S::a; double (S::*pd)[2] = &S::d; int (S::*pf)(int) const = &S::f; int (S::*pg)() = &S::g; int *ps = &S::st; int (S::*arr[2])() = {&S::g, &S::g};
std::printf("%d %g %d %d %d %d %d\n", s.*pa, (s.*pd)[1], (s.*pf)(1), (s.*pg)(), *ps, (s.*arr[1])(), (int)sizeof(pf) >= (int)sizeof(void*)); return 0; }
