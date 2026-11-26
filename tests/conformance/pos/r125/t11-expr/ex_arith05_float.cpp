// Floating point: rounding to int, division, remainder via fmod, comparisons with NaN/inf, printing precision, float vs double promotion in arithmetic, long double is 64-bit on Windows, compound assignment with floats, integer-to-float exactness.
#include <cstdio>
#include <cmath>
int main() {
    double z = 0.0; double inf = 1.0 / z; double nan = inf - inf;
    std::printf("%d %d %d %d\n", inf > 1e308, nan == nan, nan != nan, inf == inf);
    std::printf("%d %d %d\n", nan < 1, nan > 1, !(nan < 1) && !(nan > 1));
    std::printf("%.2f %.2f %.0f %.0f\n", 2.675, 1.005, 2.5, 3.5);
    std::printf("%g %g %g\n", std::fmod(7.5, 2), std::floor(-2.5), std::ceil(-2.5));
    std::printf("%d %d %d\n", (int)2.5, (int)-2.5, (int)1e9);
    float f = 16777216.0f; f += 1.0f; std::printf("%.0f\n", f);
    double d = 0.1 + 0.2; std::printf("%d %.17g\n", d == 0.3, d);
    std::printf("%d %d\n", (int)sizeof(long double), (int)sizeof(1.0f + 1.0));
    int i = 7; double q = i / 2; double r = i / 2.0; std::printf("%g %g\n", q, r);
    std::printf("%g %g %g\n", std::sqrt(2.0), std::pow(2.0, 10), std::fabs(-3.5));
    float g = 1.0f / 3.0f; std::printf("%.9g %.17g\n", g, (double)g);
    double acc = 0; for (int k = 0; k < 10; ++k) acc += 0.1; std::printf("%.17g %d\n", acc, acc == 1.0);
    std::printf("%g %g\n", 1e-320, 1e308 * 10);
    std::printf("%d\n", (long long)9007199254740993.0 == 9007199254740992LL);
    return 0;
}
