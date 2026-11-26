#include <cstdio>
#include <cmath>
int main() {
    float f = std::abs(-1.5f);
    double d = std::abs(-2.25);
    long double l = std::abs(-4.5L);
    std::printf("%g %g %g %d %d\n", (double)f, d, (double)l, (int)sizeof(std::abs(-1.5f)), (int)sizeof(std::abs(-1.5)));
    return 0;
}
