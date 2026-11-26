#include <cstdio>
#include <utility>
#include <cstddef>
template <class... Ts> void sink(Ts &&...) {} template <class... Ts> struct Cnt { static const int v = sizeof...(Ts); };
template <class... Ts> struct Pair2 { template <class... Us> struct With { static const int v = Cnt<Ts...>::v * 10 + Cnt<Us...>::v; }; };
template <class F, class... Args> auto apply(F f, Args &&... args) -> decltype(f(std::forward<Args>(args)...)) { return f(std::forward<Args>(args)...); }
template <class... Ts> void cap(Ts... ts) { auto l = [ts...] { return sizeof...(ts); }; std::printf("%d ", (int)l()); } template <class... Ts> struct Al { alignas(Ts...) char c; };
template <class... Ts> int szs() { int a[] = {(int)sizeof(Ts)...}; return a[sizeof...(Ts) - 1]; } int add3(int a, int b, int c) { return a + b + c; }
int main() { sink(1, 2.0, "s"); cap(1, 2); std::printf("%d %d %d %d\n", Pair2<int, char>::With<double>::v, apply(add3, 1, 2, 3), szs<char, long long>(), (int)alignof(Al<char, double>)); return 0; }
