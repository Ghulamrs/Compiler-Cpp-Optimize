#include <cstdio>
enum E { e0 }; enum class S { s0 }; int f(E) { return 1; } int f(int) { return 2; } int g(bool) { return 1; } int g(int) { return 2; } int h(S) { return 1; } int h(int) { return 2; } int k(char) { return 1; } int k(wchar_t) { return 2; } int k(int) { return 3; } int m(long double) { return 1; } int m(double) { return 2; } int m(float) { return 3; }
int main() { std::printf("%d %d %d %d %d %d %d %d %d %d %d\n", f(e0), f(0), g(true), g(1), h(S::s0), h(0), k('a'), k(L'a'), k(u'a' + 0), m(1.0L), m(1.0f)); return 0; }
