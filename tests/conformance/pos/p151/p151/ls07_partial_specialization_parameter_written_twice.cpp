// [temp.class.spec.match]/2: a parameter written twice deduces one value - C<N, N> matches C<2, 2>, not C<0, 2>.
#include <cstdio>
#include <cstddef>
template <int I, int N> struct C { static int v() { return 1; } };
template <int N> struct C<N, N> { static int v() { return 2; } };
template <std::size_t I, std::size_t N> struct Walk { static int count() { return 1 + Walk<I + 1, N>::count(); } };
template <std::size_t N> struct Walk<N, N> { static int count() { return 0; } };
template <class A, class B> struct E { static int v() { return 1; } };
template <class A> struct E<A, A> { static int v() { return 2; } };
int main() {
    std::printf("%d %d %d %d %d %d\n", C<0, 2>::v(), C<2, 2>::v(), Walk<0, 3>::count(), Walk<2, 2>::count(),
                E<int, char>::v(), E<int, int>::v());
    return 0;
}
