#include <cstdio>
template <class T, class U = int> struct X;
template <class T, class U> struct X { static const int v = sizeof(U); };
template <class T, int N = 3> struct Y { int a[N]; int n() const { return N; } };
template <class T> struct Y<T *, 2> { int p() const { return 7; } };
template <class... Ts> struct L { static const int v = 0; };
template <class T> struct L<T> { static const int v = 1; };
template <class T> struct Z { template <class U = T> U get(U u) const { return u; } int T2; };
template <int N> struct W { int scaled(int x) const { return x * N; } };
template <class T> T twice(T t) { return t + t; }
template <class T> T twice(T *t) { return *t * 3; }
int main() {
    Y<char> y;
    Y<int *, 2> yp;
    Z<long> z;
    W<4> w;
    int k = 5;
    std::printf("%d %d %d %d %d %ld %d %d %d\n", X<char>::v, y.n(), yp.p(), L<int>::v, L<int, int>::v, z.get(9L),
                w.scaled(2), twice(3), twice(&k));
    return 0;
}
