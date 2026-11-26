#include <cstdio>
struct B {}; struct D : B {}; struct E : D {};
int f(const int *) { return 1; } int f(int *) { return 2; } int g(B *) { return 1; } int g(D *) { return 2; } int g(void *) { return 3; } int h(const B &) { return 1; } int h(const D &) { return 2; } int k(B &) { return 1; } int k(const B &) { return 2; } int m(int &) { return 1; } int m(int &&) { return 2; } int m(const int &) { return 3; }
int main() { int i = 1; const int ci = 2; E e; std::printf("%d %d %d %d %d %d %d %d %d\n", f(&i), f(&ci), g(&e), h(e), k(e), k(static_cast<const B&>(e)), m(i), m(1), m(ci)); return 0; }
