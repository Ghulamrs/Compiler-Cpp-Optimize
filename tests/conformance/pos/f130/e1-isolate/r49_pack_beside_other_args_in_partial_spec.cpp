#include <cstdio>
template <class... Ts> struct Tup; template <> struct Tup<> { static const int n = 0; }; template <class H, class... T> struct Tup<H, T...> { static const int n = 1 + Tup<T...>::n; };
int main() { std::printf("%d\n", Tup<int, char, double>::n); return 0; }
