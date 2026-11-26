// [class.cdtor]/4: a virtual call in a base's constructor and destructor, made through its virtual base while the object is bigger - the vtordisp corrects `this`
#include <cstdio>
struct V { int v; V() : v(1) {} virtual int f() { return v; } virtual ~V() {} };
struct B : virtual V { int b; B() : b(20) { std::printf("B:%d ", f()); } ~B() { std::printf("~B:%d ", f()); } int f() { return b + v; } };
struct A : virtual V { int a; A() : a(300) {} };
struct C : A, B { int c; C() : c(4000) { std::printf("C:%d ", f()); } ~C() { std::printf("~C:%d ", f()); } int f() { return c + b + a + v; } };
struct D : C { int d; D() : d(50000) { std::printf("D:%d ", f()); } };
int main() { { D x; V *pv = &x; std::printf("| %d | ", pv->f()); } std::printf("\n"); return 0; }
