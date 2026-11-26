#include <cstdio>
struct B {}; struct D : B {};
int f(int *) { return 1; } int f(bool) { return 2; } int g(void *) { return 1; } int g(B *) { return 2; } int h(const void *) { return 1; } int h(const char *) { return 2; } int k(int *) { return 1; } int k(int) { return 2; } int m(B *) { return 1; } int m(bool) { return 2; } int n(const B *) { return 1; } int n(D *) { return 2; }
int main() { int i; D d; std::printf("%d %d %d %d %d %d %d %d\n", f(&i), f(nullptr), g(&d), h("s"), k(0), k(nullptr), m(&d), n(&d)); return 0; }
