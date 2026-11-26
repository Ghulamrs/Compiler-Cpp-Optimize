#include <cstdio>
#include <cmath>
#include <cfloat>
int main() {
    int q = 0;
    double r = std::remquo(10.0, 3.0, &q);
    std::printf("%.17g %.17g %d %.17g %.17g %.17g %.17g\n", std::remainder(10.0, 3.0), r, q, std::fma(2.0, 3.0, 0.5), std::fmax(1.0, 2.0), std::fmin(1.0, 2.0), std::fdim(5.0, 3.0));
    std::printf("%.17g %.17g %.17g %.17g %.17g %d %.17g %.17g\n", std::exp2(10.0), std::expm1(1e-10), std::log2(1024.0), std::log1p(1e-10), std::logb(1000.0), std::ilogb(1000.0), std::scalbn(1.5, 4), std::scalbln(1.5, 4L));
    std::printf("%.17g %.17g %.17g %.17g %.17g\n", std::cbrt(27.0), std::hypot(3.0, 4.0), std::asinh(1.0), std::acosh(2.0), std::atanh(0.5));
    std::printf("%.17g %.17g %.17g %.17g\n", std::erf(0.5), std::erfc(0.5), std::tgamma(5.0), std::lgamma(10.0));
    std::printf("%ld %lld %.17g %ld %lld %.17g\n", std::lround(2.5), std::llround(-2.5), std::rint(2.5), std::lrint(3.5), std::llrint(-3.5), std::nearbyint(4.5));
    std::printf("%.17g %.17g %.17g %d\n", std::copysign(3.0, -0.0), std::nextafter(1.0, 2.0), std::nexttoward(1.0, 0.0L), (int)std::isnan(std::nan("")));
    double zero = 0.0, tiny = DBL_MIN / 4, inf = INFINITY, nan = NAN;
    std::printf("%d %d %d %d %d\n", std::fpclassify(zero) == FP_ZERO, std::fpclassify(tiny) == FP_SUBNORMAL, std::fpclassify(1.0) == FP_NORMAL, std::fpclassify(inf) == FP_INFINITE, std::fpclassify(nan) == FP_NAN);
    std::printf("%d %d %d %d %d\n", (int)std::isnormal(1.0), (int)std::isnormal(tiny), (int)std::isnormal(zero), (int)std::isnormal(inf), (int)std::isnormal(nan));
    std::printf("%d %d %d %d %d %d %d %d\n", (int)std::isgreater(2.0, 1.0), (int)std::isgreater(nan, 1.0), (int)std::isgreaterequal(1.0, 1.0), (int)std::isless(1.0, 2.0), (int)std::islessequal(nan, nan), (int)std::islessgreater(1.0, 2.0), (int)std::isunordered(1.0, nan), (int)std::isunordered(1.0, 2.0));
    std::printf("%f %d %d %d %g %g\n", (double)INFINITY, (int)std::signbit(NAN), (int)std::isnan(NAN), (int)(HUGE_VALF == INFINITY), (double)HUGE_VALL, HUGE_VAL);
    std::printf("%d %d %d %d %d %d\n", MATH_ERRNO, MATH_ERREXCEPT, (int)sizeof(std::float_t), (int)sizeof(std::double_t), FLT_EVAL_METHOD, DECIMAL_DIG >= 17);
    return 0;
}
