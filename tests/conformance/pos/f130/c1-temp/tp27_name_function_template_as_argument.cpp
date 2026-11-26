#include <cstdio>
template <class T> T twice(T x) { return x * 2; } int apply(int (*f)(int), int x) { return f(x); } template <class F> int apply2(F f) { return f(3); }
int main() { int (*p)(int) = twice; std::printf("%d %d %d\n", apply(twice, 1), apply(twice<int>, 2), apply2(twice<int>)); (void)p; return 0; }
