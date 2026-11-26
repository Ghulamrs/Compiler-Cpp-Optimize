#include <cstdio>
int f(int) { return 1; } int f(long) { return 2; } int f(double) { return 3; } int g(unsigned) { return 1; } int g(long long) { return 2; } int h(char) { return 1; } int h(int) { return 2; } int k(float) { return 1; } int k(double) { return 2; } int m(long) { return 1; } int m(long long) { return 2; }
enum E { e0 }; enum class SC { s0 }; int n(int) { return 1; } int n(E) { return 2; } int q(int) { return 1; } int q(unsigned) { return 2; }
int main() { short s = 1; float fl = 1; unsigned char uc = 1; std::printf("%d %d %d %d %d %d %d %d %d %d\n", f(s), f(fl), f('c'), f(true), h(uc), k(1.0f), m(1l), n(e0), q(uc), q(1u)); return 0; }
