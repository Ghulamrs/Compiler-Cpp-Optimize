#include <cstdio>
template <int N> struct F { static const int v = F<N - 1>::v + 1; }; template <> struct F<0> { static const int v = 0; };
int main() { std::printf("%d\n", F<200>::v); return 0; }
