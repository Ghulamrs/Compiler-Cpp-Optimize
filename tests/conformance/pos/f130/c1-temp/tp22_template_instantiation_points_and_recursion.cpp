#include <cstdio>
template <int N> struct Fact { static const long long v = N * Fact<N - 1>::v; }; template <> struct Fact<0> { static const long long v = 1; }; template <int N> struct Fib { enum { v = Fib<N - 1>::v + Fib<N - 2>::v }; }; template <> struct Fib<0> { enum { v = 0 }; }; template <> struct Fib<1> { enum { v = 1 }; };
template <int N> int depth() { return 1 + depth<N - 1>(); } template <> int depth<0>() { return 0; }
int main() { std::printf("%lld %d %d\n", Fact<15>::v, (int)Fib<20>::v, depth<100>()); return 0; }
