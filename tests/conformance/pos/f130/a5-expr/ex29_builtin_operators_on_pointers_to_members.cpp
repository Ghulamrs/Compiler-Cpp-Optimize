#include <cstdio>
struct S { int a, b; };
int main() { int S::*p = &S::a; int S::*q = &S::b; int S::*n = nullptr; std::printf("%d %d %d %d\n", (int)(p == q), (int)(p != q), (int)(n == nullptr), (int)!n); return 0; }
