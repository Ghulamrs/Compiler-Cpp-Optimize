// [class.base.init]/6 delegating constructors; [class.inhctor] inheriting constructors; order of member init; init of base with delegated args.
#include <cstdio>
struct D {
    D() : D(1, 2) { std::printf("D() "); }
    D(int a) : D(a, a * 10) { std::printf("D(int) "); }
    D(int a, int b) : x(a), y(b) { std::printf("D(int,int) "); }
    int x, y;
};
struct Base { Base(int v) : v(v) {} Base(int a, int b) : v(a * b) {} Base(const char *) : v(-1) {} int v; };
struct Derived : Base { using Base::Base; int extra = 5; };
struct Derived2 : Base { using Base::Base; Derived2(double) : Base(99) {} };
struct Trace { Trace(const char *n) { std::printf("%s ", n); } };
struct Order { Trace a; Trace b; Order() : b("b"), a("a") {} };
int main() {
    D d0; std::printf("%d %d\n", d0.x, d0.y);
    D d1(3); std::printf("%d %d\n", d1.x, d1.y);
    D d2(4, 5); std::printf("%d %d\n", d2.x, d2.y);
    Derived e1(7), e2(2, 3), e3("s");
    std::printf("%d %d %d %d\n", e1.v, e2.v, e3.v, e1.extra);
    Derived2 f1(8), f2(1.0);
    std::printf("%d %d\n", f1.v, f2.v);
    Order o; std::printf("\n");
    return 0;
}
