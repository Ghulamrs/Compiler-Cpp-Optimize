#include <cstdio>
struct A { int &r; int v; };
int main() { int x = 1; A a{x, 2}; a.r = 7; A b = {x, 3}; b.r += 1; std::printf("%d %d %d\n", x, a.v, b.v); return 0; }
