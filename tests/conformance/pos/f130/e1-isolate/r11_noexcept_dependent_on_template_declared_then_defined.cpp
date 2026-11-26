#include <cstdio>
template <class X> void t(X) noexcept(noexcept(X())); template <class X> void t(X) noexcept(noexcept(X())) {}
int main() { t(1); std::printf("%d\n", (int)noexcept(t(1))); return 0; }
