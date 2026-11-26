#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <type_traits>
int main() {
    std::printf("%d %d %d %d %d %d\n",
        (int)std::is_same<decltype(std::abs(-1)), int>::value,
        (int)std::is_same<decltype(std::abs(-1L)), long>::value,
        (int)std::is_same<decltype(std::abs(-1LL)), long long>::value,
        (int)std::is_same<decltype(std::abs(-1.0f)), float>::value,
        (int)std::is_same<decltype(std::abs(-1.0)), double>::value,
        (int)std::is_same<decltype(std::abs(-1.0L)), long double>::value);
    long long big = -(1LL << 40);
    std::printf("%lld %lld %ld %d %g %g %g %d\n", std::abs(big), std::llabs(big), std::abs(-2147483647L), std::abs(-5),
                (double)std::abs(-2.5f), std::abs(-0.0), (double)std::abs(-3.25L), (int)std::signbit(std::abs(-0.0)));
    std::div_t a = std::div(17, -5);
    std::ldiv_t b = std::div(-17L, 5L);
    std::lldiv_t c = std::div(-(1LL << 40) - 7, 1000LL);
    std::lldiv_t d = std::lldiv(123456789012LL, 1000LL);
    std::ldiv_t e = std::ldiv(-7L, 2L);
    std::printf("%d %d %ld %ld %lld %lld %lld %lld %ld %ld\n", a.quot, a.rem, b.quot, b.rem, c.quot, c.rem, d.quot, d.rem, e.quot, e.rem);
    return 0;
}
