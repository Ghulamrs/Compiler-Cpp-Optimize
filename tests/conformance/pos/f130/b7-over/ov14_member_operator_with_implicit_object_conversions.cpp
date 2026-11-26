#include <cstdio>
struct S { int v; S(int v) : v(v) {} bool operator==(const S &o) const { return v == o.v; } S operator+(const S &o) const { return S(v + o.v); } }; bool operator<(const S &a, const S &b) { return a.v < b.v; }
int main() { S s(1); std::printf("%d %d %d %d\n", (int)(s == 1), (int)(1 < s), (int)(s < 2), (s + 2).v); return 0; }
