#include <cstdio>
template <int N> struct S { static const int v = S<N - 1>::v + 1; };
template <> struct S<0> { static const int v = 0; };
int main() { std::printf("%d\n", S<1024>::v); return 0; }
