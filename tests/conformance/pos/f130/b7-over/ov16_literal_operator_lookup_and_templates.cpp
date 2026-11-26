#include <cstdio>
namespace lit { constexpr long operator"" _m(unsigned long long v) { return (long)v * 1000; } constexpr long operator"" _m(long double v) { return (long)(v * 1000); } } using namespace lit;
namespace other { template <char... C> constexpr int operator"" _d() { return sizeof...(C); } }
int main() { using other::operator"" _d; std::printf("%ld %ld %d\n", 5_m, 2.5_m, 1234_d); return 0; }
