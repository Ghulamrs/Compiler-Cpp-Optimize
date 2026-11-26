// template <class T> f(T &) called with a const lvalue deduces T = const int ([temp.deduct.call]/3 with /4).
#include <cstdio>
#include <vector>
template <class T> int byRef(T &t) { return (int)sizeof(T) + (int)(t > 0 ? 1 : 0); }
template <class C> int sizeOf(C &c) { return (int)c.size(); }
template <class T> const char *kind(T &) { return "T&"; }
int main() { const int ci = 7; const std::vector<int> v(3); int i = 1; std::printf("%d %d %s %s\n", byRef(ci), sizeOf(v), kind(ci), kind(i)); return 0; }
