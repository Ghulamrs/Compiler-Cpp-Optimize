#include <cstdio>
struct A { int v = 1; int f() { return 1; } }; struct B { int v = 2; int f() { return 2; } }; struct C : A, B { int g() { return A::v + B::v + A::f() + B::f(); } };
int main() { C c; A &a = c; B &b = c; std::printf("%d %d %d %d\n", c.g(), a.v, b.v, c.A::v); return 0; }
