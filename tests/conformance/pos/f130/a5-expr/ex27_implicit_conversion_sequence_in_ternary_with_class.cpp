#include <cstdio>
struct S { int v; S(int x) : v(x) {} };
int main() { bool c = false; S s = c ? S(1) : 2; S t = c ? 3 : S(4); std::printf("%d %d\n", s.v, t.v); return 0; }
