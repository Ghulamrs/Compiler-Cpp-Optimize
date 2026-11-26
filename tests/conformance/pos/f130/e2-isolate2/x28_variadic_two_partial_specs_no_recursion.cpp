#include <cstdio>
template <class T, class... Ts> struct A { static const int v = 0; }; template <class T, class U, class... Ts> struct A<T, U, Ts...> { static const int v = 1; }; template <class T, class... Ts> struct A<T, T, Ts...> { static const int v = 2; };
int main() { std::printf("%d %d %d\n", A<int>::v, A<int, char>::v, A<int, int>::v); return 0; }
