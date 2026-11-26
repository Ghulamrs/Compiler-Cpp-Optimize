#include <cstdio>
struct S { mutable int hits = 0; int v = 1; int get() const { hits++; return v; } };
int main() { const S s; s.get(); s.get(); std::printf("%d %d\n", s.hits, s.v); return 0; }
