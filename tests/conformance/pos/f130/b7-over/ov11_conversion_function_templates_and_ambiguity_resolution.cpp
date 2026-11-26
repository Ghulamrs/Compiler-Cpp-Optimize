#include <cstdio>
struct Any { template <class T> operator T() const { return T(7); } }; struct Num { operator int() const { return 1; } operator long() const { return 2; } };
int f(long) { return 1; } int g(int) { return 1; } int g(long) { return 2; }
int main() { Any a; Num n; int i = a; double d = a; std::printf("%d %g %d %d %d\n", i, d, f(n), g(static_cast<int>(n)), (int)(long)n); return 0; }
