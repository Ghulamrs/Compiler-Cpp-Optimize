// Direct-list-initialisation of scalars: int b{5}; and in various positions.
#include <cstdio>
int main() {
    int a{}; std::printf("%d ", a);
    int b{5}; std::printf("%d ", b);
    double d{2.5}; std::printf("%g ", d);
    char c{'q'}; std::printf("%c ", c);
    unsigned u{7u}; std::printf("%u ", u);
    bool f{true}; std::printf("%d ", f);
    int *p{nullptr}; std::printf("%d\n", p == nullptr);
    return 0;
}
