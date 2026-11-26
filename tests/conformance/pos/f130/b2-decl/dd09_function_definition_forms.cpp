#include <cstdio>
struct S { int v; S(int v) : v(v) {} S() : S(7) {} S(const S &o) = default; int get() const; }; int S::get() const { return v; } S make() { return 5; } S make2() { return {}; }
int main() { std::printf("%d %d\n", make().get(), make2().v); return 0; }
