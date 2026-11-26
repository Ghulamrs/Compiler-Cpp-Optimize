// `explicit constexpr operator bool()`: a specifier between `explicit` and the conversion's name.
#include <cstdio>
struct B { int v; explicit constexpr operator bool() const { return v != 0; } };
struct C { int v; constexpr explicit operator bool() const { return v > 1; } };
int main() {
    B b{2}; B z{0}; C c{1};
    std::printf("%d %d %d %d\n", b ? 1 : 0, z ? 1 : 0, !c ? 1 : 0, static_cast<bool>(b) && !static_cast<bool>(c) ? 1 : 0);
    return 0;
}