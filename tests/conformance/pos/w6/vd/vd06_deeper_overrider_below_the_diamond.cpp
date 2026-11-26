// [class.virtual]/2: an overrider in a class below the diamond, and the deleting destructor of the whole - with constructors, so cl gives the virtual base a vtordisp
#include <cstdio>
struct V { int v; V() : v(1) {} virtual int f() { return v; } virtual ~V() {} };
struct A : virtual V { int a; A() : a(2) {} int f() { return a; } };
struct B : virtual V { int b; B() : b(3) {} };
struct C : A, B { int c; C() : c(4) {} };
struct D : C { int d; D() : d(5) {} int f() { return d + c + b + a + v; } ~D() { std::printf("~D "); } };
int main() {
    D *x = new D; V *pv = x; A *pa = x; B *pb = x;
    std::printf("%d %d %d %d ", pv->f(), pa->f(), pb->f(), x->f());
    delete pv;
    std::printf("\n");
    return 0;
}
