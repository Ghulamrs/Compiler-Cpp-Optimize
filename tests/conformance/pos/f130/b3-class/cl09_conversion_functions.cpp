#include <cstdio>
struct S { int v; operator int() const { return v; } operator const char *() const { return "s"; } explicit operator bool() const { return v != 0; } template <class T> operator T*() const { return nullptr; } };
typedef int (*FP)(int); int twice(int x) { return x * 2; } struct Callable { operator FP() const { return twice; } };
int main() { S s{3}; int i = s; const char *p = s; double *dp = s; Callable c; std::printf("%d %s %d %d %d\n", i, p, (int)(dp == nullptr), c(4), s ? 1 : 0); return 0; }
