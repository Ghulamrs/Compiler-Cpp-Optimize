#include <cstdio>
struct S { int v; explicit operator bool() const { return v != 0; } }; S get(int v) { return S{v}; } int cnt = 3; int next() { return cnt--; }
int main() { if (S s = get(2)) std::printf("%d ", s.v); else std::printf("no %d", s.v); if (int *p = nullptr) (void)p; else std::printf("null "); while (int n = next()) std::printf("%d", n); for (int i = 0; S s = get(i < 2 ? 1 : 0); i++) std::printf("i"); std::printf("\n"); return 0; }
