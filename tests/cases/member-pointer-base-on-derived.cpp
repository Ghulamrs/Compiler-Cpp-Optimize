// **[expr.mptr.oper]/3: the object is converted to the member pointer's
// class first.** A `&B2::f` or `&B2::b` applied to a D whose B2 base is not
// at offset 0 has to move to that base before the pair's own adjustment or
// the member's offset is added; both paths read B1's field until 2026-09-14
// (Fable 5.1's second review, finding 1).
extern "C" int printf(const char *, ...);
struct B1 { int a; B1() : a(10) {} int fa() { return a; } virtual int v() { return 1000 + a; } };
struct B2 { int b; B2() : b(20) {} int fb() { return b; } virtual int w() { return 2000 + b; } };
struct D : B1, B2 { int c; D() : c(30) {} int w() { return 3000 + c; } };
int main() {
    D d; D *pd = &d;
    int (B2::*pf)() = &B2::fb;
    int B2::*pm = &B2::b;
    int (B2::*pv)() = &B2::w;
    printf("%d %d %d %d\n", (d.*pf)(), (pd->*pf)(), d.*pm, pd->*pm);
    B2 &r = d;
    printf("%d %d %d %d\n", (r.*pf)(), r.*pm, (d.*pv)(), (r.*pv)());
    const D &cd = d;
    int (B1::*pa)() = &B1::fa;
    printf("%d %d\n", cd.*pm, (d.*pa)());
    return 0;
}
