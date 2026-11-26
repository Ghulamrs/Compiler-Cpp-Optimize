#include <cstdio>
#include <utility>
template <class... Ts> struct Holder { static_assert(sizeof...(Ts) > 0, "need one"); int n; template <class... Us> Holder(Us &&... us) : n(sizeof...(Us)) {} };
template <class T, class... Ts> struct AllSame { static const bool v = true; }; template <class T, class U, class... Ts> struct AllSame<T, U, Ts...> { static const bool v = false; }; template <class T, class... Ts> struct AllSame<T, T, Ts...> { static const bool v = AllSame<T, Ts...>::v; };
int main() { Holder<int> h(1, 2, 3); Holder<int, char> k; std::printf("%d %d %d %d\n", h.n, k.n, (int)AllSame<int, int, int>::v, (int)AllSame<int, int, char>::v); return 0; }
