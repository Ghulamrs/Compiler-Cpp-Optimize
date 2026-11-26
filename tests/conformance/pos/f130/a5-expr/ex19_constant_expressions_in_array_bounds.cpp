#include <cstdio>
constexpr int sq(int x) { return x * x; } enum { E = 4 }; const int c = 3; struct S { static const int n = 2; static constexpr int m = 5; };
int main() { int a[sq(2)]; int b[E + c]; int d[S::n * S::m]; int e[sizeof(int) == 4 ? 1 : 2]; int f['b' - 'a' + 1]; int g[(1, 2) + 1]; std::printf("%d %d %d %d %d %d\n", (int)sizeof(a) / 4, (int)sizeof(b) / 4, (int)sizeof(d) / 4, (int)sizeof(e) / 4, (int)sizeof(f) / 4, (int)sizeof(g) / 4); return 0; }
