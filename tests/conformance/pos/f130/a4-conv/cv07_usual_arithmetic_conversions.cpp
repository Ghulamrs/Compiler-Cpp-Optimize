#include <cstdio>
#include <type_traits>
int main() { std::printf("%d %d %d %d %d %d %d\n", (int)std::is_same<decltype(1 + 1u), unsigned>::value, (int)std::is_same<decltype(1l + 1u), long>::value, (int)std::is_same<decltype(1ll + 1ul), long long>::value, (int)std::is_same<decltype('a' + 'b'), int>::value, (int)std::is_same<decltype(1.0f + 1), float>::value, (int)std::is_same<decltype(1.0L + 1.0), long double>::value, (int)std::is_same<decltype(true + true), int>::value);
std::printf("%d %d %d\n", (int)std::is_same<decltype((short)1 + (short)1), int>::value, (int)std::is_same<decltype((unsigned short)1 + 1), int>::value, (int)(-1 < 1u)); return 0; }
