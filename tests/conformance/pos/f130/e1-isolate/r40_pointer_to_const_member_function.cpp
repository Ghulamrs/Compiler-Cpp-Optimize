#include <cstdio>
struct S { int v = 3; int f(int x) const { return v + x; } };
int main() { S s; int (S::*pf)(int) const = &S::f; std::printf("%d\n", (s.*pf)(1)); return 0; }
