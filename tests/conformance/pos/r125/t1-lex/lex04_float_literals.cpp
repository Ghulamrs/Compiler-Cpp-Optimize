#include <cstdio>
int main() {
    std::printf("%d %d %d\n", (int)sizeof(1.0), (int)sizeof(1.0f), (int)sizeof(1.0L));
    std::printf("%g %g %g %g\n", 1e3, 1.5e-3, .5, 5.);
    std::printf("%.17g\n", 0.1);
    std::printf("%.9g\n", 0.1f);
    std::printf("%g %g\n", 1E+2, 0x10 + 0.5);
    double big = 1e308 * 10;
    std::printf("%d\n", big > 1e308);
    std::printf("%.3f %.3f\n", 2.5, -2.5);
    std::printf("%d %d\n", (int)2.9, (int)-2.9);
    std::printf("%g\n", 1.0 / 3.0);
    return 0;
}
