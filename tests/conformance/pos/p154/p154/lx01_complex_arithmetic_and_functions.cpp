#include <complex>
#include <cstdio>
#include <iostream>
#include <sstream>
typedef std::complex<double> C;
int main() {
    C a(3, 4), b(1, -2);
    C s = a + b, d = a - b, p = a * b, q = a / b;
    std::printf("%g %g | %g %g | %g %g | %g %g\n", s.real(), s.imag(), d.real(), d.imag(), p.real(), p.imag(), q.real(), q.imag());
    std::printf("%g %g %g\n", std::abs(a), std::norm(a), std::arg(C(0, 1)));
    C r = std::sqrt(C(-4, 0));
    std::printf("%g %g\n", r.real(), r.imag());
    C e = std::exp(C(0, 3.14159265358979323846));
    std::printf("%.6f %.6f\n", e.real(), e.imag());
    C l = std::log(C(-1, 0));
    std::printf("%.6f %.6f\n", l.real(), l.imag());
    C pw = std::pow(C(0, 1), 2);
    std::printf("%.6f %.6f\n", pw.real(), pw.imag());
    C sn = std::sin(C(1, 1)), cs = std::cos(C(1, 1)), tn = std::tan(C(1, 1));
    std::printf("%.6f %.6f %.6f %.6f %.6f %.6f\n", sn.real(), sn.imag(), cs.real(), cs.imag(), tn.real(), tn.imag());
    C as = std::asin(C(0.5, 0.25)), ac = std::acos(C(0.5, 0.25)), at = std::atan(C(0.5, 0.25));
    std::printf("%.6f %.6f %.6f %.6f %.6f %.6f\n", as.real(), as.imag(), ac.real(), ac.imag(), at.real(), at.imag());
    C ah = std::asinh(C(1, 2)), ach = std::acosh(C(1, 2)), ath = std::atanh(C(0.5, 0.5));
    std::printf("%.6f %.6f %.6f %.6f %.6f %.6f\n", ah.real(), ah.imag(), ach.real(), ach.imag(), ath.real(), ath.imag());
    std::complex<float> f(1.5f, 2.5f);
    C fromf = f;
    std::complex<float> back(fromf);
    std::printf("%g %g %d\n", fromf.real(), back.imag(), (int)sizeof(std::real(f)));
    std::cout << a << " " << std::polar(2.0, 0.0) << "\n";
    std::istringstream in("(1.5,-2) (7) 3");
    C x, y, z;
    in >> x >> y >> z;
    std::cout << x << y << z << "\n";
    std::printf("%d %d %g\n", (int)(a == C(3, 4)), (int)(C(2, 0) == 2.0), std::real(5));
    C c2 = 2.0 * a - 1.0;
    std::printf("%g %g\n", c2.real(), c2.imag());
    std::complex<long double> ld = std::pow(std::complex<float>(1, 1), 2.0L);
    std::printf("%.4f %.4f\n", (double)ld.real(), (double)ld.imag());
    return 0;
}
