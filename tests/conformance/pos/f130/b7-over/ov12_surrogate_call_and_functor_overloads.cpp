#include <cstdio>
typedef int (*FP)(int); int twice(int x) { return x * 2; } struct Conv { operator FP() const { return twice; } }; struct Func { int operator()(int x) const { return x + 1; } int operator()(const char *) const { return -1; } template <class T> int operator()(T, T) const { return 99; } };
int main() { Conv c; Func f; std::printf("%d %d %d %d\n", c(3), f(3), f("s"), f(1, 2)); return 0; }
