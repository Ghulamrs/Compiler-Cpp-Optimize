// INT_MIN handling, unsigned comparisons in loops, 64-bit multiplication/division by constants (magic numbers), modulo with powers of two for negative values, bit tricks, popcount loop.
#include <cstdio>
#include <climits>
int popcount(unsigned long long x) { int n = 0; while (x) { x &= x - 1; ++n; } return n; }
int main() {
    int mn = INT_MIN; std::printf("%d %d %d\n", mn / 2, mn % 7, mn / -2 == mn ? 1 : 0);
    std::printf("%d %d %d %d\n", -17 / 4, -17 % 4, -17 / 8, -17 % 8);
    std::printf("%d %d %d\n", -17 & 7, -17 >> 3, (-17 + 7) / 8);
    long long big = 123456789012345LL; std::printf("%lld %lld %lld %lld\n", big / 10, big % 10, big / 1000000007LL, big % 1000000007LL);
    unsigned long long ub = 0xFFFFFFFFFFFFFFFFULL; std::printf("%llu %llu %llu\n", ub / 3, ub % 10, ub / 7);
    unsigned u = 0xFFFFFFFFu; std::printf("%u %u %u\n", u / 3, u % 7, u / 1000);
    int count = 0; for (unsigned k = 10; k-- > 0;) ++count; std::printf("%d\n", count);
    std::printf("%d %d %d\n", popcount(0), popcount(0xFFULL), popcount(ub));
    std::printf("%d %d\n", INT_MAX, INT_MIN);
    std::printf("%lld %lld\n", LLONG_MAX, LLONG_MIN);
    std::printf("%u %lu %llu\n", UINT_MAX, ULONG_MAX, ULLONG_MAX);
    std::printf("%d %d\n", CHAR_MIN, CHAR_MAX);
    int x = 1; x <<= 30; x <<= 1; std::printf("%d\n", x == INT_MIN);
    short sh = SHRT_MAX; int prom = sh + 1; std::printf("%d\n", prom);
    std::printf("%d %d\n", 1000000 * 1000 / 1000, (int)(1000000LL * 1000000LL / 1000000LL));
    return 0;
}
