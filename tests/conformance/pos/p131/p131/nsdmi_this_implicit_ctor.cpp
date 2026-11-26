#include <cstdio>
struct S { int a = 4; int c = this->a * 2; int d = a + 1; };
struct Outer { int z = 99; int f() { S s; return s.c + s.d; } };
struct T : S { int e = a + c; };
int main() { Outer o; S s; T t; std::printf("%d %d %d %d\n", o.f(), s.c, s.d, t.e); return 0; }
