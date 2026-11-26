// Constant folding corner cases: unsigned wraparound in constants, shifts of constants, signed division truncation, char arithmetic promotions, float constant folding, large multiplication in long long, comparisons of mixed signedness in constants.
#include <cstdio>
int main() {
    const unsigned u = 0u - 1u;
    const int sh = 1 << 30;
    const long long big = 1000000LL * 1000000LL;
    const int neg = -7 / 2, negm = -7 % 2, pos = 7 / -2;
    const char c = 'A' + 2;
    const double dd = 1.0 / 3.0 * 3.0;
    const bool mixed = -1 < 0u;
    const int sh2 = -16 >> 2;
    const unsigned long long ull = ~0ULL >> 1;
    const int truncf = (int)3.99, truncn = (int)-3.99;
    const short sfold = (short)70000;
    const unsigned char uc = (unsigned char)300;
    std::printf("%u %d %lld %d %d %d %c\n", u, sh, big, neg, negm, pos, c);
    std::printf("%d %d %d %llu %d %d %d %d\n", dd == 1.0, mixed, sh2, ull, truncf, truncn, sfold, uc);
    std::printf("%d %d\n", (int)sizeof(u + 1), (int)sizeof('a' + 'b'));
    std::printf("%d %d\n", 1u > -1 ? 0 : 1, (unsigned char)255 + 1);
    static_assert((unsigned)-1 == 4294967295u, "wrap");
    static_assert(-7 / 2 == -3 && -7 % 2 == -1, "trunc");
    static_assert(sizeof(short) == 2 && sizeof(wchar_t) == 2, "sizes");
    return 0;
}
