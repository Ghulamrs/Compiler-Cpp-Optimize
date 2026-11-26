#include <cstdio>
constexpr unsigned long long operator"" _kb(unsigned long long v) { return v * 1024; }
long double operator"" _half(long double v) { return v / 2; }
int main() { std::printf("%llu %Lf\n", 3_kb, 5.0_half); static_assert(2_kb == 2048, "k"); return 0; }
