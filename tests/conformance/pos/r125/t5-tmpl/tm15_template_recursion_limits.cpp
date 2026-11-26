// Instantiation depth 500 (below the 1024 recommended minimum of [implimits]) and a 200-element pack; many instantiations of one template.
#include <cstdio>
template <int N> struct Deep { static const int v = Deep<N - 1>::v + 1; };
template <> struct Deep<0> { static const int v = 0; };
template <int N> int rec() { return rec<N - 1>() + 1; }
template <> int rec<0>() { return 0; }
template <class... Ts> int countAll(Ts...) { return sizeof...(Ts); }
#define T10 1, 2, 3, 4, 5, 6, 7, 8, 9, 10
#define T50 T10, T10, T10, T10, T10
#define T200 T50, T50, T50, T50
int main() {
    std::printf("%d %d\n", Deep<500>::v, rec<300>());
    std::printf("%d\n", countAll(T200));
    return 0;
}
