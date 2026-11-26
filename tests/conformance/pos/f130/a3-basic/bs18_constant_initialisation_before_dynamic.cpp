#include <cstdio>
struct K { constexpr K(int v) : v(v) {} int v; };
extern int dyn;
constexpr K k(5);
int dyn = k.v * 2;
int main() { std::printf("%d %d\n", k.v, dyn); return 0; }
