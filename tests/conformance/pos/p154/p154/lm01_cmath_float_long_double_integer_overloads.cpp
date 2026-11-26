#include <cmath>
#include <cstdio>
#include <type_traits>
int main() {
    static_assert(std::is_same<decltype(std::sqrt(2.0f)), float>::value, "f");
    static_assert(std::is_same<decltype(std::sqrt(2)), double>::value, "i");
    static_assert(std::is_same<decltype(std::sqrt(2.0L)), long double>::value, "ld");
    static_assert(std::is_same<decltype(std::pow(2.0f, 3)), double>::value, "fi");
    static_assert(std::is_same<decltype(std::pow(2.0f, 3.0f)), float>::value, "ff");
    static_assert(std::is_same<decltype(std::pow(2, 3)), double>::value, "ii");
    static_assert(std::is_same<decltype(std::atan2(1.0f, 2.0L)), long double>::value, "fld");
    static_assert(std::is_same<decltype(std::lround(2.5f)), long>::value, "lr");
    std::printf("%g %g %g %g\n", (double)std::sqrt(2.0f), std::pow(2, 10), std::hypot(3, 4.0f), (double)std::fabs(-2.5f));
    std::printf("%d %d %d\n", (int)std::isnan(std::sqrt(-1.0f)), (int)std::signbit(-0.0f), (int)std::isinf(5));
    float ip;
    float fr = std::modf(3.25f, &ip);
    std::printf("%g %g %ld %g\n", fr, ip, std::lround(2.5), std::fma(2, 3, 4));
    double (*f)(double) = std::sqrt;
    float (*g)(float) = std::sqrt;
    std::printf("%g %g\n", f(9), (double)g(16));
    return 0;
}
