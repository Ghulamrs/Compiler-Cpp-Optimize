#include <cstdio>
int main() { const char *a = "x" u8"y"; const wchar_t *b = L"a" "b"; const char16_t *c = "a" u"b"; const char32_t *d = U"a" "b" U"c";
std::printf("%s %d %d %d\n", a, (int)sizeof(L"a" "b") / (int)sizeof(wchar_t), (int)c[1], (int)sizeof(U"a" "b" U"c") / 4); (void)b; (void)d; return 0; }
