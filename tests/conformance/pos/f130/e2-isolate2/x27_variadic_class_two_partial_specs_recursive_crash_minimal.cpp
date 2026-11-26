#include <cstdio>
template <class T, class... Ts> struct A { static const bool v = true; }; template <class T, class U, class... Ts> struct A<T, U, Ts...> { static const bool v = false; }; template <class T, class... Ts> struct A<T, T, Ts...> { static const bool v = A<T, Ts...>::v; };
int main() { std::printf("%d\n", (int)A<int, int>::v); return 0; }
