// **Construction vtables and the VTT**, which exist for one thing: a virtual
// call, or a virtual base's member, from inside a base's constructor or
// destructor while the object is still being built as something more
// derived. B's constructor runs inside a C and inside a D, and each time
// `f()` reaches B's own override and `v` is where the *most derived* class
// put V - which is not where B's own table says. So the C1 of the most
// derived class hands every base's C2 a VTT: `_ZTT1D` names the address
// points, `_ZTC1D0_1C` and `_ZTC1D16_1B` are C's and B's groups laid out with
// D's virtual-base offsets, and `_ZTC1D<W>_1W` is W's, W being a virtual
// base with a virtual base of its own. The same tables in reverse for the
// destructors, and the implicit copy constructor takes the VTT like any C2.
//
// Every table this program needs is compared against clang's word for word
// on x86_64 (TMS6747.md, "Virtual inheritance"); what the program prints is
// which function each call reached and what `v` held, so a table that is
// one word out shows as a wrong number rather than a crash.
extern "C" int printf(const char *, ...);
struct V { int v; V() : v(0) {} virtual int f() { return 1; } virtual ~V() {} };
struct B : virtual V { int b; B(); virtual int g() { return 10; } int f() { return 2; } ~B() { printf("~B%d ", f()); } };
struct A { int a; virtual int h() { return 5; } A() : a(3) {} virtual ~A() {} };
struct C : A, B { int c; C(); int g() { return 20; } int f() { return 3; } ~C() { printf("~C%d ", f()); } };
struct W : virtual V { int w; W(); virtual int k() { return 7; } };
struct D : C, virtual W { int d; double pad; D(); int k() { return 8; } int g() { return 30; } };
B::B() : b(1) { v = 11; printf("B:%d,%d,%d ", f(), g(), v); }
C::C() : c(2) { v += 100; printf("C:%d,%d,%d,%d ", f(), g(), h(), v); }
W::W() : w(4) { printf("W:%d,%d,%d ", f(), k(), v); }
D::D() : d(9), pad(1.5) { printf("D:%d,%d,%d,%d ", f(), g(), k(), v); }
int main() {
    { B bb; printf("| %d\n", bb.f()); }
    { C cc; V *pv = &cc; B *pb = &cc; printf("| %d %d %d %d\n", pv->f(), pb->f(), pb->g(), pv->v); }
    { D dd; V *pv = &dd; W *pw = &dd; B *pb = &dd;
      printf("| %d %d %d %d %d %d\n", pv->f(), pw->f(), pw->k(), pb->g(), pb->f(), pv->v);
      D copy(dd); V *cv = &copy; printf("| %d %d\n", cv->f(), cv->v); }
    printf("\n");
    { V *p = new D; printf("| %d ", p->f()); delete p; printf("\n"); }
    { B *p = new C; printf("| %d ", p->g()); delete p; printf("\n"); }
    return 0;
}
