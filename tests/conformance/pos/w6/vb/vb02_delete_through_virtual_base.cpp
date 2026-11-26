// [expr.delete]/3, [class.dtor]: a destructor entered at the virtual base, through V*, through a branch and directly
#include <cstdio>
struct V { virtual ~V() { std::printf("~V "); } };
struct A : virtual V { int a; ~A() { std::printf("~A "); } };
struct B : virtual V { int b; };
struct C : A, B { int c; ~C() { std::printf("~C "); } };
int main() {
    V *p = new C; delete p; std::printf("| ");
    A *q = new C; delete q; std::printf("| ");
    B *r = new C; delete r; std::printf("| ");
    { C local; } std::printf("|\n");
    return 0;
}
