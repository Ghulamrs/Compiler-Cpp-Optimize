// 64-bit multiply, add with carry patterns, mixed 32/64 arithmetic, shifts across widths, unsigned long long comparisons, multiplication overflow wrap (unsigned), 128-bit style manual multiply.
#include <cstdio>
unsigned long long mulhi(unsigned long long a, unsigned long long b) { unsigned long long a0 = a & 0xFFFFFFFFULL, a1 = a >> 32, b0 = b & 0xFFFFFFFFULL, b1 = b >> 32; unsigned long long p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0, p11 = a1 * b1; unsigned long long mid = (p00 >> 32) + (p01 & 0xFFFFFFFFULL) + (p10 & 0xFFFFFFFFULL); return p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32); }
int main() {
    unsigned long long a = 0xDEADBEEFCAFEBABEULL, b = 0x123456789ABCDEF0ULL;
    std::printf("%llx %llx %llx\n", a * b, a + b, a - b);
    std::printf("%llx\n", mulhi(a, b));
    std::printf("%llx %llx %llx\n", a >> 7, a << 9, (a >> 60) | (b << 4));
    int i = -5; unsigned long long m = i; long long l = (long long)i * 3000000000LL; std::printf("%llx %lld\n", m, l);
    unsigned u = 0xFFFFFFFFu; unsigned long long w = u; unsigned long long w2 = (unsigned long long)u * u; std::printf("%llu %llu\n", w + 1, w2);
    std::printf("%d %d %d\n", a > b, (long long)a < (long long)b, a == 0xDEADBEEFCAFEBABEULL);
    long long neg = -1; std::printf("%llx %d\n", (unsigned long long)neg >> 1, (int)(neg >> 1));
    unsigned long long c = 1; for (int k = 0; k < 70; ++k) c *= 3; std::printf("%llu\n", c);
    unsigned long long cnt = 0; for (unsigned long long k = 0xFFFFFFFFFFFFFFF0ULL; k != 0; ++k) ++cnt; std::printf("%llu\n", cnt);
    std::printf("%lld %lld\n", (long long)(int)0x80000000u, (long long)(unsigned)0x80000000u);
    return 0;
}
