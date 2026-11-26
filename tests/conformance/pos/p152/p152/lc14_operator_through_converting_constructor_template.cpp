#include <cstdio>
#include <type_traits>
namespace N {
template <bool C> struct It {
    int v;
    explicit It(int x) : v(x) {}
    template <bool D, class = typename std::enable_if<C && !D>::type> It(const It<D> &o) : v(o.v) {}
    It operator-(int n) const { return It(v - n); }
};
inline int operator-(const It<true> &a, const It<true> &b) { return a.v - b.v; }
inline bool operator==(const It<true> &a, const It<true> &b) { return a.v == b.v; }
struct P { int v; };
struct Q { int v; Q(const P &p) : v(p.v) {} };
inline int operator-(const Q &a, const Q &b) { return a.v - b.v; }
}
int main() {
    N::It<false> a(10);
    N::It<true> b(3);
    N::P p = { 7 }, q = { 2 };
    std::printf("%d\n", p - q);
    std::printf("%d %d %d\n", a - b, b - b, (int)(a == a));
}
