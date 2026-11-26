#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <type_traits>
int main() { std::printf("%d %d %d %d %d %d\n", (int)std::is_same<decltype(std::sqrt(4)), double>::value, (int)std::is_same<decltype(std::pow(2.0f, 2)), double>::value, (int)std::is_same<decltype(std::abs(-1)), int>::value, (int)std::is_same<decltype(std::abs(-1L)), long>::value, (int)std::is_same<decltype(std::abs(-1LL)), long long>::value, (int)std::is_same<decltype(std::abs(-1.0f)), float>::value); std::printf("%g %g %d %g\n", std::pow(2, 3), std::floor(-1.5), (int)std::abs(-1LL), std::fabs(-2.0f)); return 0; }
