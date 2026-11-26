// [class.virtual]/2: an overrider in a class below the diamond, and the deleting destructor of the whole
#include <cstdio>
struct V { int v = 1; virtual int f() { return v; } virtual ~V() {} };
struct A : virtual V { int a = 2; int f() { return a; } };
struct B : virtual V { int b = 3; };
struct C : A, B { int c = 4; };
struct D : C { int d = 5; int f() { return d + c + b + a + v; } };
int main() {
    D *x = new D; V *pv = x; A *pa = x; B *pb = x;
    std::printf("%d %d %d %d ", pv->f(), pa->f(), pb->f(), x->f());
    delete pv;
    std::printf("\n");
    return 0;
}
