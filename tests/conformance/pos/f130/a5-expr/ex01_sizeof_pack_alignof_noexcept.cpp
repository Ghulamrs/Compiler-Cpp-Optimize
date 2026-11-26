#include <cstdio>
template <class... T> int n() { return sizeof...(T); }
void f() noexcept; void g(); struct S { ~S() noexcept(false); S(); }; S::S() {} S::~S() noexcept(false) {} void f() noexcept {} void g() {}
int main() { std::printf("%d %d %d %d %d %d %d\n", n<>(), n<int, char, S>(), (int)alignof(double), (int)noexcept(f()), (int)noexcept(g()), (int)noexcept(S()), (int)noexcept(1 + 1)); return 0; }
