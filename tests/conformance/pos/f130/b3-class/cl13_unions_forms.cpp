#include <cstdio>
#include <cstring>
union U { int i; float f; char c[4]; }; union Tagged { struct { int kind; int val; } a; struct { int kind; double d; } b; }; union Ctor { int i; double d; Ctor(int x) : i(x) {} Ctor(double x) : d(x) {} };
int main() { U u; u.i = 0x41424344; Tagged t; t.a.kind = 1; t.a.val = 7; Ctor c(2.5); Ctor d(3); std::printf("%c %d %d %g %d %d %d\n", u.c[0], t.b.kind, t.a.val, c.d, d.i, (int)sizeof(U), (int)sizeof(Tagged)); return 0; }
