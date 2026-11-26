// static_assert with a constexpr call at block scope and at namespace scope.
#include <cstdio>
constexpr int fact(int n) { return n <= 1 ? 1 : n * fact(n - 1); }
static_assert(fact(5) == 120, "ns");
struct S { static_assert(fact(3) == 6, "class"); };
int main() { static_assert(fact(4) == 24, "block"); constexpr int c = fact(6); static_assert(c == 720, "var"); std::printf("%d\n", c); return 0; }
