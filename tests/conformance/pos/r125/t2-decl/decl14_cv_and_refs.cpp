// [dcl.ref]: references to const bind temporaries and extend their lifetime; reference collapsing via typedef; const members; mutable; volatile skipped (cpp11 downgrades).
#include <cstdio>
struct T { T(int v) : v(v) { std::printf("T%d\n", v); } ~T() { std::printf("~T%d\n", v); } int v; };
T make(int v) { return T(v); }
struct M { mutable int hits; int val; M() : hits(0), val(1) {} int get() const { ++hits; return val; } };
typedef int &IR; typedef const int CI;
template <class R> struct Kind { static const int k = 0; };
template <class R> struct Kind<R &> { static const int k = 1; };
template <class R> struct Kind<R &&> { static const int k = 2; };
int main() {
    { const T &r = make(1); std::printf("bound %d\n", r.v); }
    std::printf("after\n");
    { T &&rr = make(2); rr.v = 22; std::printf("rr %d\n", rr.v); }
    const M m; m.get(); m.get(); std::printf("%d\n", m.hits);
    int x = 1; IR r1 = x; const IR r2 = x; CI *pc = &x; (void)pc;
    r1 = 2; std::printf("%d %d\n", x, r2);
    std::printf("%d %d %d\n", Kind<IR>::k, Kind<IR &>::k, Kind<IR &&>::k);
    const int &ci = 5 + 6; std::printf("%d\n", ci);
    const double &cd = 7; std::printf("%g\n", cd);
    const char *const cp = "x"; std::printf("%s\n", cp);
    return 0;
}
