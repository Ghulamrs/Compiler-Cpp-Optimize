#include <cstdio>
#include <cstdlib>
[[noreturn]] void stop() { std::exit(0); } [[]] int x = 1; int [[]] y = 2; struct [[]] S { [[]] int m; }; void f(int [[]] p [[]]) { (void)p; } [[carries_dependency]] int *g(int *p [[carries_dependency]]) { return p; }
int main() { S s{3}; f(1); int v = 4; std::printf("%d %d %d %d\n", x, y, s.m, *g(&v)); stop(); }
