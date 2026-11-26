#include <cstdio>
auto f() -> int { return 1; } auto g(int x) -> decltype(x * 2.0) { return x * 2.0; } template <class A, class B> auto add(A a, B b) -> decltype(a + b) { return a + b; } auto h() -> int (*)(int); int twice(int x) { return x * 2; } auto h() -> int (*)(int) { return twice; }
struct S { auto m() const -> int { return 5; } auto n() -> S & { return *this; } }; auto l = []() -> long { return 6; };
int main() { S s; std::printf("%d %g %g %d %d %d %ld\n", f(), g(2), add(1, 2.5), h()(3), s.m(), s.n().m(), l()); return 0; }
