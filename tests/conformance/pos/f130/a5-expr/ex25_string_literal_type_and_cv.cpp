#include <cstdio>
#include <type_traits>
int main() { std::printf("%d %d %d\n", (int)std::is_same<decltype("ab"), const char(&)[3]>::value, (int)std::is_same<decltype(L"ab"), const wchar_t(&)[3]>::value, (int)std::is_same<decltype(u8"ab"), const char(&)[3]>::value); return 0; }
