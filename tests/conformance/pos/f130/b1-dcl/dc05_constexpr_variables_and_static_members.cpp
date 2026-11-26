#include <cstdio>
struct S { static constexpr int n = 3; static constexpr double d = 1.5; static const int k = 7; static constexpr const char *name = "s"; enum { E = n * 2 }; };
constexpr S s{}; constexpr int &&r = 5; constexpr const char *txt = "hello";
int main() { int a[S::n]; const int *p = &S::k; std::printf("%d %g %d %d %s %c %d %d\n", S::n, S::d, S::k, S::E, S::name, txt[1], (int)sizeof(a) / 4, *p); (void)s; return 0; }
