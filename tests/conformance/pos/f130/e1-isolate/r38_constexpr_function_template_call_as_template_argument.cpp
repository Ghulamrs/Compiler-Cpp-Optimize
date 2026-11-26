#include <cstdio>
template <class T> constexpr T sq(T x) { return x * x; } template <int N> struct Arr { int a[N]; }; constexpr int sqi(int x) { return x * x; }
int main() { Arr<sqi(3)> a; Arr<sq(3)> b; std::printf("%d %d\n", (int)sizeof(a) / 4, (int)sizeof(b) / 4); return 0; }
