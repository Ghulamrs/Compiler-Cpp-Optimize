// [conv], [expr]/9-10: usual arithmetic conversions - signed/unsigned mixing, promotions of small types, long/unsigned on LLP64, float/double, comparisons after conversion.
#include <cstdio>
int main() {
    int i = -1; unsigned u = 1; long l = -1; unsigned long ul = 1; long long ll = -1; unsigned long long ull = 1;
    std::printf("%d %d %d %d\n", i < u, l < ul, ll < ull, i < ull);
    std::printf("%d %d %d\n", (int)sizeof(i + u), (int)sizeof(l + ull), (int)sizeof(u + ll));
    short s = -1; unsigned short us = 1; char c = -1; unsigned char uc = 255;
    std::printf("%d %d %d %d\n", s < us, c < uc, s + us, c + uc);
    std::printf("%d %d\n", (int)sizeof(s + s), (int)sizeof(uc * uc));
    float f = 1.5f; double d = 2.5; std::printf("%d %g %d\n", (int)sizeof(f + 1), f * d, (int)sizeof(f * d));
    std::printf("%u %u\n", (unsigned)-1 / 2, 7u - 8u);
    std::printf("%lld %llu\n", (long long)(unsigned)-1, (unsigned long long)-1LL);
    bool b = 3; std::printf("%d %d %d\n", b, true + true, (int)sizeof(true + true));
    std::printf("%d %d\n", 'a' + 1, (char)('a' + 1));
    unsigned char uc2 = 200; uc2 += 100; std::printf("%d\n", uc2);
    signed char sc = 127; sc++; std::printf("%d\n", sc);
    std::printf("%d %d\n", (int)(long)2147483648LL, (int)(unsigned)4294967295u);
    double big = 1e10; std::printf("%lld %u\n", (long long)big, (unsigned)65536.9);
    std::printf("%g %g\n", (double)(float)0.1, (float)1e20);
    return 0;
}
