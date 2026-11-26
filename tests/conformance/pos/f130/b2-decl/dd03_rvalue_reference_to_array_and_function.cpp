#include <cstdio>
int twice(int x) { return x * 2; } template <class T, int N> int cnt(T (&&)[N]) { return N; } template <class T, int N> int cnt(T (&)[N]) { return -N; }
int main() { int (&&ra)[3] = {1, 2, 3}; int (&rf)(int) = twice; int a[2]; std::printf("%d %d %d %d\n", ra[2], rf(2), cnt({1, 2, 3, 4}), cnt(a)); return 0; }
