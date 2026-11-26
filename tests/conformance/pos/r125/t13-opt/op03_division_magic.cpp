// Division and modulo by constants (signed and unsigned, 32 and 64-bit, powers of two and not) with negative dividends - the magic-number paths must round toward zero.
#include <cstdio>
int d3(int x) { return x / 3; } int m3(int x) { return x % 3; } int d7(int x) { return x / 7; } int m7(int x) { return x % 7; }
int d8(int x) { return x / 8; } int m8(int x) { return x % 8; } int dm3(int x) { return x / -3; } int mm5(int x) { return x % -5; }
unsigned ud3(unsigned x) { return x / 3; } unsigned um7(unsigned x) { return x % 7; } unsigned ud10(unsigned x) { return x / 10; }
long long ld3(long long x) { return x / 3; } long long lm7(long long x) { return x % 7; } long long ld1e9(long long x) { return x / 1000000007LL; } unsigned long long uld5(unsigned long long x) { return x / 5; }
long long ld16(long long x) { return x / 16; } long long lm16(long long x) { return x % 16; }
int d1(int x) { return x / 1; } int dminus1(int x) { return x / -1; }
int main() {
    int xs[] = {0, 1, -1, 7, -7, 8, -8, 100, -100, 2147483647, -2147483647};
    for (int x : xs) std::printf("%d %d %d %d %d %d %d %d\n", d3(x), m3(x), d7(x), m7(x), d8(x), m8(x), dm3(x), mm5(x));
    unsigned us[] = {0u, 1u, 7u, 100u, 4294967295u, 2147483648u};
    for (unsigned u : us) std::printf("%u %u %u\n", ud3(u), um7(u), ud10(u));
    long long ls[] = {0LL, -1LL, 1000000007LL, -9223372036854775807LL, 9223372036854775807LL, -17LL};
    for (long long l : ls) std::printf("%lld %lld %lld %lld %lld\n", ld3(l), lm7(l), ld1e9(l), ld16(l), lm16(l));
    unsigned long long ul = 18446744073709551615ULL; std::printf("%llu %llu\n", uld5(ul), uld5(7ULL));
    std::printf("%d %d\n", d1(-5), dminus1(5));
    return 0;
}
