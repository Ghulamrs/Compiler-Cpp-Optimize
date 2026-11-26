// [class.mi], [class.virtual]: a class adding its own virtual beside a polymorphic virtual base; members read through adjusted `this` - with constructors, so cl gives the virtual base a vtordisp
#include <cstdio>
struct V { int v; V() : v(3) {} virtual int f() { return v; } };
struct P : virtual V { int p; P() : p(4) {} virtual int g() { return p * 10 + v; } int f() { return p + v * 100; } };
struct Q : P { int q; Q() : q(5) {} int g() { return q + p + v; } };
int main() {
    Q x; V *pv = &x; P *pp = &x;
    std::printf("%d %d %d %d %d\n", pv->f(), pp->g(), x.f(), x.g(), (int)(sizeof(Q) > sizeof(P)));
    return 0;
}
