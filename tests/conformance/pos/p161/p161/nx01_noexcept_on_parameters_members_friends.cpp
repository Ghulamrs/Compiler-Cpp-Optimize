// [except.spec]: a friend declaration takes an exception specification, and `noexcept` on a pointer to function stays
// with it for [expr.unary.noexcept] - a parameter's, a parameter of function type's, a data member's, a base's member's.
#include <cstdio>
void g() noexcept {}
void h() {}
struct S { void (*p)() noexcept; void (*q)(); };
struct D : S {};
void take(void (*a)() noexcept, void b() noexcept, void (*c)()) {
    const int x = noexcept(a());
    const int y = noexcept(b());
    const int z = noexcept(c());
    std::printf("%d %d %d\n", x, y, z);
}
void fr() noexcept;
struct F { friend void fr() noexcept; friend int fq(const F &) noexcept { return 1; } };
void fr() noexcept {}
int main() {
    S s = { g, h };
    D d;
    d.p = g;
    d.q = h;
    S *ps = &s;
    F f;
    const int a = noexcept(s.p());
    const int b = noexcept(s.q());
    const int c = noexcept(ps->p());
    const int e = noexcept(d.p());
    const int i = noexcept(fr());
    const int j = noexcept(fq(f));
    std::printf("%d %d %d %d %d %d\n", a, b, c, e, i, j);
    take(g, g, h);
    return 0;
}
