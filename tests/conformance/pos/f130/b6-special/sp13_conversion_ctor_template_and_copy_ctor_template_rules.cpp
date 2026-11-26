#include <cstdio>
struct S { int v; S(int v) : v(v) {} template <class T> S(const T &t) : v((int)sizeof(T) * 100) {} S(const S &o) : v(o.v + 1) {} };
int main() { S a(1); S b(a); S c(2.5); S d('c'); std::printf("%d %d %d %d\n", a.v, b.v, c.v, d.v); return 0; }
