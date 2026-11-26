// [expr.mptr.oper]: pointers to data members and member functions (incl. virtual, in bases, through derived), .* and ->*, comparisons, arrays of them, null.
#include <cstdio>
struct B { int b; virtual int vf() { return 1; } int nf() { return 2; } B() : b(10) {} virtual ~B() {} };
struct D : B { int d; int vf() override { return 3; } D() : d(20) {} };
struct MD : D { int m; int md; MD() : md(30) {} };
int main() {
    int B::*pb = &B::b; int D::*pd = &D::d; int D::*pbd = &B::b;
    int (B::*vf)() = &B::vf; int (B::*nf)() = &B::nf;
    D d; B *bp = &d; MD md;
    std::printf("%d %d %d %d %d\n", d.*pb, d.*pd, d.*pbd, (bp->*vf)(), (bp->*nf)());
    std::printf("%d %d %d\n", md.*pd, md.*pbd, (md.*vf)());
    int MD::*pmd = &MD::md; int MD::*pm = &MD::m; md.m = 5;
    std::printf("%d %d\n", md.*pmd, md.*pm);
    int B::*null = nullptr; std::printf("%d %d %d\n", null == nullptr, pb == &B::b, pb != null);
    int B::*arr[2] = {&B::b, nullptr}; std::printf("%d %d\n", d.*arr[0], arr[1] == nullptr);
    int (B::*fns[2])() = {&B::vf, &B::nf}; std::printf("%d %d\n", (d.*fns[0])(), (d.*fns[1])());
    std::printf("%d %d\n", (int)sizeof(pb) >= 4, (int)sizeof(vf) >= 8);
    return 0;
}
