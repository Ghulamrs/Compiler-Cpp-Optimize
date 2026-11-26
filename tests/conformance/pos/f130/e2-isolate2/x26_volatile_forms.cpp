#include <cstdio>
volatile int g = 1; static volatile int s = 2; struct S { volatile int m = 3; int get() const volatile { return 4; } };
int main() { volatile int l = 5; S x; std::printf("%d %d %d %d %d\n", g, s, x.m, l, x.get()); return 0; }
