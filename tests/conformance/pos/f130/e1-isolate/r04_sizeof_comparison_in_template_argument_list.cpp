#include <cstdio>
#include <type_traits>
template <class T> struct W { static const bool small = std::integral_constant<bool, sizeof(T) == 1>::value; };
int main() { std::printf("%d %d\n", (int)W<char>::small, (int)W<int>::small); return 0; }
