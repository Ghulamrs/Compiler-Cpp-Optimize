// Floating-point results must not change with -O2: no reassociation, no contraction visible in results, conversions preserved, comparisons with equality after arithmetic, float vs double intermediates, division by zero yields inf not a trap.
#include <cstdio>
double sumThree(double a, double b, double c) { return a + b + c; }
float fma_like(float a, float b, float c) { return a * b + c; }
double divz(double a, double b) { return a / b; }
int toInt(double d) { return (int)d; }
float narrow(double d) { return (float)d; }
int main() {
    std::printf("%.17g %.17g\n", sumThree(1e16, 1.0, -1e16), sumThree(1e16, -1e16, 1.0));
    std::printf("%.9g\n", fma_like(1.0000001f, 1.0000001f, -1.0000002f));
    std::printf("%g %g %d\n", divz(1, 0), divz(-1, 0), divz(0, 0) != divz(0, 0));
    std::printf("%d %d %d\n", toInt(2.9999999999), toInt(-0.5), toInt(1e9 + 0.5));
    std::printf("%.9g %.17g\n", narrow(0.1), (double)narrow(0.1));
    float f = 0.1f; double d = f; std::printf("%d %d\n", d == 0.1, f == 0.1f);
    double x = 1.0; for (int i = 0; i < 10; ++i) x *= 1.1; std::printf("%.17g\n", x);
    std::printf("%.17g\n", 0.1 + 0.2 + 0.3);
    std::printf("%.17g\n", 0.1 + (0.2 + 0.3));
    return 0;
}
