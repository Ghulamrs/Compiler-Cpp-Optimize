// Pack expansions in a base-specifier-list, in a mem-initializer-list, and in a braced-init-list.
#include <cstdio>
struct A { int a = 1; }; struct B { int b = 2; };
template <class... Bs> struct Multi : Bs... { int sum() const { return sumOf(static_cast<const Bs &>(*this)...); } static int sumOf() { return 0; } template <class H, class... R> static int sumOf(const H &h, const R &... r) { return h.value() + sumOf(r...); } };
struct X { int value() const { return 10; } }; struct Y { int value() const { return 20; } };
template <class... Ts> int count(Ts... ts) { int arr[] = {0, ts...}; return (int)(sizeof(arr) / sizeof(int)) - 1; }
int main() { Multi<X, Y> m; std::printf("%d %d\n", m.sum(), count(1, 2, 3)); return 0; }
