// Runtime (non-constant) 64-bit division, remainder and shifts with negative operands, through functions that defeat folding.
#include <cstdio>
long long id(long long v) { return v; }
int main() { long long a = id(-9000000000LL), b = id(7), c = id(-13); std::printf("%lld %lld %lld %lld %lld %lld\n", a / b, a % b, a / c, a % c, a >> 3, (a * c) >> 5); unsigned long long u = (unsigned long long)id(-1); std::printf("%llu %llu %llu\n", u / 3, u % 10, u >> 33); return 0; }
