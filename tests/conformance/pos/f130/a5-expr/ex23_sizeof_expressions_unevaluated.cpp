#include <cstdio>
int side = 0; int f() { side++; return 1; } struct Inc { int v = 5; };
int main() { int n = sizeof(f()); int m = sizeof(Inc().v); bool ne = noexcept(f()); std::printf("%d %d %d %d\n", n, m, side, (int)ne); return 0; }
