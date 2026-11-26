// [temp.param]: non-type parameter packs (int... Ns), sizeof...(Ns), expansion into arrays, recursion over values, mixed type/non-type templates, template template parameters.
#include <cstdio>
template <int... Ns> struct Sum { static const int v = 0; };
template <int N, int... Ns> struct Sum<N, Ns...> { static const int v = N + Sum<Ns...>::v; };
template <int... Ns> int countNs() { return sizeof...(Ns); }
template <int... Ns> int sumArr() { int arr[] = {Ns..., 0}; int s = 0; for (int x : arr) s += x; return s; }
template <class T, template <class> class Holder> struct Apply { Holder<T> h; };
template <class T> struct Box { T v; int tag() const { return 1; } };
template <class T> struct Crate { T v; int tag() const { return 2; } };
template <template <class> class H> int tagOf() { H<int> h; return h.tag(); }
template <class T, T V> struct Const { static const T value = V; };
template <bool B> struct BoolK { static const int v = B ? 1 : 0; };
int main() {
    std::printf("%d %d %d\n", Sum<1, 2, 3>::v, Sum<>::v, Sum<10>::v);
    std::printf("%d %d\n", countNs<1, 2, 3, 4>(), countNs<>());
    std::printf("%d\n", sumArr<5, 6, 7>());
    Apply<int, Box> a; a.h.v = 9; Apply<double, Crate> c; c.h.v = 1.5;
    std::printf("%d %g %d %d\n", a.h.v, c.h.v, tagOf<Box>(), tagOf<Crate>());
    std::printf("%d %c %d\n", Const<int, 42>::value, Const<char, 'q'>::value, BoolK<(2 > 1)>::v);
    return 0;
}
