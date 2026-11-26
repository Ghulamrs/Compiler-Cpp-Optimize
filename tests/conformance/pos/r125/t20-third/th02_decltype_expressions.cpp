// decltype of arbitrary expressions: binary, unary, call results, conditional, comma, member call.
#include <cstdio>
template <class A, class B> struct Same { static const bool v = false; };
template <class A> struct Same<A, A> { static const bool v = true; };
struct S { int m() const { return 1; } double d; };
int main() {
    int x = 1; double y = 2; S s;
    std::printf("%d %d %d %d\n", Same<decltype(x + 1), int>::v, Same<decltype(x * y), double>::v, Same<decltype(-x), int>::v, Same<decltype(!x), bool>::v);
    std::printf("%d %d %d %d\n", Same<decltype(x < y), bool>::v, Same<decltype(s.m()), int>::v, Same<decltype(s.d * 2), double>::v, Same<decltype(x ? x : y), double>::v);
    decltype(x + y) z = x + y; std::printf("%g\n", z);
    return 0;
}
