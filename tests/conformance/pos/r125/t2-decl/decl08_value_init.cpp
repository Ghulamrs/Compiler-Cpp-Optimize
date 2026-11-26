// [dcl.init]/7-8: T() and T{} zero-initialise PODs, call user ctors, zero members a non-user-provided ctor does not initialise; new T() vs new T; arrays with fewer initialisers zero the rest.
#include <cstdio>
struct Pod { int a; double b; int *p; };
struct Def { Def() {} int a; };
struct Mixed { int a; Pod p; };
struct Ctor { Ctor() : v(3) {} int v; };
struct WithCtorMember { Ctor c; int x; };
template <class T> T valueInit() { return T(); }
int main() {
    Pod p = Pod(); std::printf("%d %g %d\n", p.a, p.b, p.p == 0);
    Pod q{}; std::printf("%d %g %d\n", q.a, q.b, q.p == 0);
    Pod *hp = new Pod(); std::printf("%d %d\n", hp->a, hp->p == 0); delete hp;
    Mixed m = Mixed(); std::printf("%d %d %g\n", m.a, m.p.a, m.p.b);
    Mixed m2{}; std::printf("%d %d\n", m2.a, m2.p.a);
    WithCtorMember w = WithCtorMember(); std::printf("%d %d\n", w.c.v, w.x);
    WithCtorMember w2{}; std::printf("%d %d\n", w2.c.v, w2.x);
    int arr[4] = {1}; std::printf("%d %d %d\n", arr[0], arr[1], arr[3]);
    Pod parr[2] = {{1, 1.0, 0}}; std::printf("%d %g\n", parr[1].a, parr[1].b);
    int z = valueInit<int>(); double dz = valueInit<double>(); Pod pz = valueInit<Pod>();
    std::printf("%d %g %d %g\n", z, dz, pz.a, pz.b);
    int *np = new int(); std::printf("%d\n", *np); delete np;
    int *arrp = new int[3](); std::printf("%d %d\n", arrp[0], arrp[2]); delete[] arrp;
    static Pod sp; std::printf("%d\n", sp.a);
    char buf[8] = ""; std::printf("%d %d\n", buf[0], buf[7]);
    char cs[4] = "ab"; std::printf("%d %d\n", cs[2], cs[3]);
    return 0;
}
