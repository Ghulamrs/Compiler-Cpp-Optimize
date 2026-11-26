// [class.base.init]/10: the most derived class initialises virtual bases; intermediate initialisers for them are ignored; virtual base with constructor args; data through virtual base.
#include <cstdio>
struct V { V(int x) : x(x) { std::printf("V(%d) ", x); } int x; };
struct A : virtual V { A() : V(1) { std::printf("A "); } };
struct B : virtual V { B() : V(2) { std::printf("B "); } };
struct C : A, B { C() : V(3) { std::printf("C "); } };
struct D : C { D() : V(4) { std::printf("D "); } };
int main() {
    { C c; std::printf("%d\n", c.x); }
    { D d; std::printf("%d\n", d.x); A &a = d; std::printf("%d\n", a.x); }
    return 0;
}
