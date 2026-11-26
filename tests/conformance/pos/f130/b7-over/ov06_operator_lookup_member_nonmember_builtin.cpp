#include <cstdio>
struct S { int v; int operator+(int) const { return 1; } }; int operator+(int, const S &) { return 2; } int operator+(const S &, const S &) { return 3; } struct T { operator int() const { return 10; } }; namespace N { struct U { int v; }; int operator-(U a, U b) { return a.v - b.v; } enum E { e1 = 1, e2 = 2 }; int operator|(E a, E b) { return (int)a * 10 + (int)b; } }
int main() { S s{0}; T t; N::U u1{5}, u2{3}; std::printf("%d %d %d %d %d %d %d\n", s + 1, 1 + s, s + s, t + 1, 2 * t, u1 - u2, N::e1 | N::e2); return 0; }
