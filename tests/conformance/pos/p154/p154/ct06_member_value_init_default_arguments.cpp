#include <cstdio>
#include <random>
struct S { int v; explicit S(int x = 5) : v(x) {} };
struct T { S s; T() : s() {} };
template <class E> struct U { E e; U() : e() {} };
template <class X> struct V { X x; template <class Q> explicit V(Q &q) : x(1) { (void)q; } explicit V(X v = 7) : x(v) {} };
struct W { V<int> e; W() : e() {} };
template <class E> struct Z { E e; unsigned n; Z() : e(), n(0) {} };
int main() {
    T t;
    U<S> u;
    W w;
    V<int> v;
    std::printf("%d %d %d %d\n", t.s.v, u.e.v, w.e.x, v.x);
    Z<std::ranlux24_base> z;
    std::ranlux24_base b;
    std::printf("%u %u\n", (unsigned)z.e(), (unsigned)b());
    U<std::ranlux24_base> ub;
    std::printf("%u\n", (unsigned)ub.e());
    return 0;
}
