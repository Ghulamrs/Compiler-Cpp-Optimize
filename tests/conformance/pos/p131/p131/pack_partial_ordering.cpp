#include <cstdio>
template <class... Ts> struct L { static const int v = 0; };
template <class T, class... Ts> struct L<T, Ts...> { static const int v = 1; };
template <class T> struct L<T> { static const int v = 2; };
template <class T, class... Ts> struct L<T *, Ts...> { static const int v = 3; };
template <class... Ts> struct M { static const int v = 0; };
template <class... Ts> struct M<int, Ts...> { static const int v = 1; };
template <class... Ts> struct M<int, char, Ts...> { static const int v = 2; };
int main() { std::printf("%d %d %d %d %d %d %d %d\n", L<>::v, L<int>::v, L<int, char>::v, L<int *, char>::v, L<int *>::v, M<int>::v, M<int, char>::v, M<int, char, long>::v); return 0; }
