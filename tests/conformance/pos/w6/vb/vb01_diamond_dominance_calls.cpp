// [class.virtual]/2, [class.member.lookup]: a virtual base's function overridden on one branch, reached through every base
#include <cstdio>
struct V { int v = 1; virtual int f() const { return v; } virtual int g() const { return 100; } };
struct A : virtual V { int a = 10; int f() const { return a + v; } };
struct B : virtual V { int b = 20; };
struct C : A, B { int c = 30; };
int main() {
    C c; V *pv = &c; A *pa = &c; B *pb = &c;
    std::printf("%d %d %d %d %d %d\n", pv->f(), pa->f(), pb->f(), c.f(), pb->g(), c.V::f());
    return 0;
}
