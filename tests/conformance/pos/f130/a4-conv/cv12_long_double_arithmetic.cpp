#include <cstdio>
#include <cmath>
int main() { long double a = 1.0L / 3.0L; long double b = std::sqrt(2.0L); double d = a; long double c = a * 3.0L; std::printf("%.10Lf %.6Lf %.6f %Lg %d\n", a, b, d, c, (int)sizeof(a)); return 0; }
