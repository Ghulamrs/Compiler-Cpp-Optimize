#include <cstdio>
struct S { static const int n = 5; static constexpr int m = 6; static const long long big = 1LL << 40; static const bool flag = true; static const char ch = 'z'; }; const int S::n;
int take(const int &r) { return r; }
int main() { int a[S::n]; std::printf("%d %d %d %lld %d %c %d\n", S::n, take(S::n), S::m, S::big, (int)S::flag, S::ch, (int)sizeof(a) / 4); return 0; }
