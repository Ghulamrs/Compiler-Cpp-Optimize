#include <cstdio>
template <class T> int k(T) { return 0; } template <> int k(int) { return 1; } template <> int k(unsigned) { return 2; } template <> int k(long) { return 3; }
template <> int k(unsigned long) { return 4; } template <> int k(long long) { return 5; } template <> int k(unsigned long long) { return 6; }
template <> int k(float) { return 7; } template <> int k(double) { return 8; } template <> int k(long double) { return 9; }
int main() { std::printf("%d %d %d %d %d %d %d %d %d %d %d\n", k(1), k(1u), k(1l), k(1ul), k(1ll), k(1ull), k(1.0f), k(1.0), k(1.0L), k(0x7fffffff), k(2147483648)); return 0; }
