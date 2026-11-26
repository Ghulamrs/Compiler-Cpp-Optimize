#include <cstdio>
struct B { int v; B(int v) : v(v) {} B(int a, int b) : v(a + b) {} B(const char *) : v(-1) {} protected: B(double d) : v((int)d * 10) {} };
struct D : B { using B::B; int w = 7; }; struct E : D { using D::D; };
int main() { D d1(5); D d2(1, 2); D d3("s"); E e(3); std::printf("%d %d %d %d %d\n", d1.v, d2.v, d3.v, d1.w, e.v); return 0; }
