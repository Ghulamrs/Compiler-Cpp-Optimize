#include <cstdio>
struct S { int f(int) { return 1; } int f(double) { return 2; } static int s() { return 3; } static int sv; }; int S::sv = 4;
int main() { S s; int (S::*p1)(int) = &S::f; int (S::*p2)(double) = &S::f; int (*ps)() = &S::s; std::printf("%d %d %d %d %d\n", (s.*p1)(1), (s.*p2)(1.0), ps(), s.s(), s.sv); return 0; }
