#include <cstdio>
#include <utility>
template <class T, class U> auto add(T t, U u) -> decltype(t + u) { return t + u; } template <class C> auto front(C &c) -> decltype(c[0]) { return c[0]; } template <class T> auto declv() -> decltype(std::declval<T>().size()); template <class T> struct Res { typedef decltype(T() + T()) type; };
template <class F, class A> auto call(F f, A a) -> decltype(f(a)) { return f(a); } int twice(int x) { return x * 2; }
int main() { int arr[2] = {1, 2}; front(arr) = 9; Res<short>::type r = 3; std::printf("%g %d %d %d\n", add(1, 2.5), arr[0], (int)sizeof(r), call(twice, 4)); return 0; }
