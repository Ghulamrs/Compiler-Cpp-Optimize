// Object layout against cl: sizeof/offsets with virtuals, multiple and virtual inheritance, empty bases (EBO under the Microsoft ABI), pointer adjustment on conversion, this in multiple bases.
#include <cstdio>
#include <cstddef>
struct Empty {};
struct E1 : Empty { int x; };
struct E2 : Empty { Empty e; int x; };
struct V { virtual void f() {} int a; };
struct W { virtual void g() {} int b; };
struct VW : V, W { int c; };
struct VB : virtual V { int d; };
struct Dia1 : virtual V { int p; }; struct Dia2 : virtual V { int q; }; struct Dia : Dia1, Dia2 { int r; };
struct Plain { char c; int i; char d; short s; };
struct Bits { unsigned a : 3; unsigned b : 5; unsigned c : 9; int d : 1; };
struct LL { char c; long long l; };
struct Dbl { char c; double d; char e; };
int main() {
    std::printf("%d %d %d %d\n", (int)sizeof(Empty), (int)sizeof(E1), (int)sizeof(E2), (int)offsetof(E2, x));
    std::printf("%d %d %d %d\n", (int)sizeof(V), (int)sizeof(VW), (int)sizeof(VB), (int)sizeof(Dia));
    std::printf("%d %d %d %d\n", (int)sizeof(Plain), (int)offsetof(Plain, i), (int)offsetof(Plain, s), (int)sizeof(Bits));
    std::printf("%d %d %d %d\n", (int)sizeof(LL), (int)offsetof(LL, l), (int)sizeof(Dbl), (int)offsetof(Dbl, e));
    VW vw; V *pv = &vw; W *pw = &vw;
    std::printf("%d %d\n", (void *)pv == (void *)&vw, (int)((char *)pw - (char *)&vw));
    VW *back = static_cast<VW *>(pw); std::printf("%d\n", back == &vw);
    Dia dia; dia.a = 7; Dia1 *d1 = &dia; Dia2 *d2 = &dia; V *v1 = d1; V *v2 = d2;
    std::printf("%d %d %d\n", v1 == v2, v1->a, (char *)v1 - (char *)&dia > 0);
    Bits bt; bt.a = 7; bt.b = 31; bt.c = 511; bt.d = -1;
    std::printf("%u %u %u %d\n", bt.a, bt.b, bt.c, bt.d);
    std::printf("%d\n", (int)sizeof(VB) >= (int)sizeof(V) + 8);
    return 0;
}
