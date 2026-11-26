#include <cstdio>
template <class T> struct A { static const int v = 1; }; template <int N> struct B { static const int v = N; };
int main() { int i = 1, j = 2; int k = i+++j; A<B<2>> a; B<(1>2)> b; std::printf("%d %d %d %d %d\n", k, i, j, a.v + B<2>::v, b.v); return 0; }
