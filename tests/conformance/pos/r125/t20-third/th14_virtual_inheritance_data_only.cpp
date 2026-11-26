// Virtual inheritance without virtual functions (diamond), on Windows: sizes, one shared base, conversion through both paths.
#include <cstdio>
struct V { int v; V() : v(1) {} };
struct A : virtual V { int a; A() : a(2) {} };
struct B : virtual V { int b; B() : b(3) {} };
struct D : A, B { int d; D() : d(4) {} };
int main() { D d; d.v = 9; A *pa = &d; B *pb = &d; V *va = pa; V *vb = pb; std::printf("%d %d %d %d %d\n", va == vb, va->v, pb->v, d.a + d.b + d.d, (int)sizeof(D) > (int)sizeof(A)); return 0; }
