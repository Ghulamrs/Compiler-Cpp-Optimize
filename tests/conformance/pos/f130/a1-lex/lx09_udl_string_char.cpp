#include <cstdio>
#include <cstddef>
std::size_t operator"" _n(const char *, std::size_t n) { return n; }
std::size_t operator"" _n(const wchar_t *, std::size_t n) { return n + 100; }
std::size_t operator"" _n(const char16_t *, std::size_t n) { return n + 200; }
std::size_t operator"" _n(const char32_t *, std::size_t n) { return n + 300; }
int operator"" _c(char c) { return c; } int operator"" _c(wchar_t c) { return c + 1000; }
int main() { std::printf("%d %d %d %d %d %d\n", (int)"abc"_n, (int)L"ab"_n, (int)u"a"_n, (int)U""_n, 'x'_c, L'x'_c); return 0; }
