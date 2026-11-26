#include <cstdio>
int f(char) { return 1; } int f(wchar_t) { return 2; } int f(char16_t) { return 3; } int f(char32_t) { return 4; } int f(int) { return 5; }
int main() { std::printf("%d %d %d %d %d\n", f('a'), f(L'a'), f(u'a'), f(U'a'), f(65)); return 0; }
