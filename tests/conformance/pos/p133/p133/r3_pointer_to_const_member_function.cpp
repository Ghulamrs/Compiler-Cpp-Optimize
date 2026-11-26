#include <cstdio>
struct S {
    int v;
    int f() { return 1 + v; }
    int f() const { return 2 + v; }
    int g(int x) const { return x * v; }
    virtual int h() const { return 7; }
};
struct D : S { int h() const { return 8; } };
typedef int (S::*CF)() const;
using NF = int (S::*)();
template <class T> struct Holder { T p; };
template <int (S::*PF)(int) const> int via(const S &s, int x) { return (s.*PF)(x); }
int pick(int (S::*)() const) { return 10; }
int pick(int (S::*)()) { return 20; }
int main() {
    S s; s.v = 3; const S cs = s; D d; d.v = 1;
    CF a = &S::f; NF b = &S::f; int (S::*g)(int) const = &S::g; int (S::*h)() const = &S::h;
    const S *cp = &cs; const S &dr = d;
    std::printf("%d %d %d %d %d\n", (s.*a)(), (s.*b)(), (cs.*a)(), (cp->*g)(4), (dr.*h)());
    Holder<int (S::*)(int) const> hp = { &S::g };
    std::printf("%d %d %d %d\n", (cs.*hp.p)(2), via<&S::g>(cs, 5), pick(a), pick(b));
    return 0;
}
