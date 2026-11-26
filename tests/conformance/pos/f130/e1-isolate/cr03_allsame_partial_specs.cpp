#include <cstdio>
template <class T, class... Ts> struct AllSame { static const bool v = true; }; template <class T, class U, class... Ts> struct AllSame<T, U, Ts...> { static const bool v = false; }; template <class T, class... Ts> struct AllSame<T, T, Ts...> { static const bool v = AllSame<T, Ts...>::v; };
int main() { std::printf("%d %d\n", (int)AllSame<int, int, int>::v, (int)AllSame<int, int, char>::v); return 0; }
