// [expr.const]/5 and a constexpr conversion function: a class constant where an integral one is wanted,
// and `P()` of a class with no constructor written, value-initialised.
#include <cstdio>
struct Two { constexpr operator int() const { return 2; } };
struct Size { int v; constexpr Size(int x) : v(x) {} constexpr operator int() const { return v; } };
struct Z { int a; double b; };
constexpr Size three(3);
static_assert(Two() == 2, "two");
static_assert(Z().a == 0, "zero");
int a[Two()];
int b[three];
int c[Size(4) + 1];
int main() {
    std::printf("%d %d %d %d\n", (int)(sizeof(a) / sizeof(int)), (int)(sizeof(b) / sizeof(int)),
                (int)(sizeof(c) / sizeof(int)), (int)Two());
    return 0;
}