#include <cstdio>
namespace N {
template <bool C> struct K { int v; explicit K(int x) : v(x) {} K(const K<!C> &o) : v(o.v) {} };
inline int operator*(const K<true> &a, const K<true> &b) { return a.v * b.v; }
inline int times(const K<true> &a, const K<true> &b) { return a.v * b.v + 1; }
}
int main() {
    N::K<false> k(3);
    N::K<true> t = k;
    std::printf("%d %d\n", t.v, times(k, k));
    std::printf("%d\n", k * k);
}
