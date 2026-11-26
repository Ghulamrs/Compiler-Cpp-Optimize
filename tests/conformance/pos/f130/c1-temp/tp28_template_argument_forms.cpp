#include <cstdio>
template <class T> struct W { static const int v = sizeof(T); }; template <int N> struct I { static const int v = N; }; struct S { static const int k = 3; enum { e = 4 }; }; constexpr int cf() { return 5; } const int ci = 6;
int main() { std::printf("%d %d %d %d %d %d %d %d\n", W<int[2]>::v, W<int (*)[2]>::v, W<void (*)()>::v, I<S::k>::v, I<S::e>::v, I<cf()>::v, I<ci + 1>::v, I<(1 > 0) ? 8 : 9>::v); return 0; }
