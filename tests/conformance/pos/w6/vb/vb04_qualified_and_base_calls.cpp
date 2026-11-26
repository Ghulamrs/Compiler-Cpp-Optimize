// [expr.call]/1, [class.virtual]/15: a qualified call to an override of a virtual base's function, from a derived override
#include <cstdio>
struct V { virtual int f(int k) { return k; } };
struct A : virtual V { int a = 7; int f(int k) { return a * k; } };
struct D : A { int d = 2; int f(int k) { return A::f(k) + d + V::f(k); } };
int main() { D x; V *pv = &x; A *pa = &x; std::printf("%d %d %d\n", pv->f(3), pa->f(4), x.A::f(5)); return 0; }
