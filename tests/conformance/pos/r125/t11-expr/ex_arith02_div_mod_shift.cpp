// [expr.mul]/4 truncation toward zero, [expr.shift] arithmetic right shift of negatives (implementation-defined, cl: arithmetic), shifts by 31/63, unsigned shifts, shifts of small types promote to int.
#include <cstdio>
int main() {
    int a = -7, b = 2;
    std::printf("%d %d %d %d\n", a / b, a % b, -a / -b, a % -b);
    std::printf("%d %d %d %d\n", 7 / -2, 7 % -2, -7 / -2, -7 % -2);
    long long la = -9000000000LL; std::printf("%lld %lld\n", la / 7, la % 7);
    std::printf("%d %d %d\n", -16 >> 2, -1 >> 31, (int)(0x80000000u >> 31));
    std::printf("%d %u %lld\n", 1 << 31 == (int)0x80000000u, 1u << 31, 1LL << 62);
    unsigned char uc = 0x80; std::printf("%d %d\n", uc << 1, (int)sizeof(uc << 1));
    int sh = 3; std::printf("%d %d %d\n", 1 << sh, 256 >> sh, -256 >> sh);
    unsigned long long ull = 0xF000000000000000ULL; std::printf("%llu %llu\n", ull >> 60, ull << 2);
    std::printf("%d %d\n", 17 % 5, -17 % 5);
    unsigned ua = 17; std::printf("%u %u\n", ua / 5, ua % 5);
    int x = 100; x /= 7; x %= 5; x <<= 2; x >>= 1; std::printf("%d\n", x);
    return 0;
}
