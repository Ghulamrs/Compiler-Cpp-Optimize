#include <cstdio>
#include <type_traits>
template <int K> struct Kind : std::integral_constant<int, K> {};
struct S {
    int how;
    S(std::integral_constant<int, 0>) : how(0) {}
    S(std::integral_constant<int, 1>) : how(1) {}
    S(std::integral_constant<int, 2>) : how(2) {}
};
int main() {
    S a((Kind<1>())), b((Kind<2>()));
    std::printf("%d %d\n", a.how, b.how);
}
