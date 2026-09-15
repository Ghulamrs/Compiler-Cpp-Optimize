// **A virtual base of a virtual base**: `D : virtual W`, `W : virtual V`.
// Two orders are measured, and they differ. The layout is the inheritance
// graph's preorder, W then V (clang lays W at 16 and V at 32, W's own V being
// laid nowhere - a virtual base contributes only its non-virtual part). The
// construction is [class.base.init]/10's: V *before* W, because W's
// constructor may read `v`, which it does here and finds V's 0, not garbage;
// W's C2 gets D's virtual VTT for it, and the read goes through the
// construction vtable `_ZTC1D16_1W`, whose vbase_offset is +16 where W's own
// table says +16 by coincidence and D's says -16 from W. The offsets are
// clang's on x86_64, and the C6000 has the same shape at half the width.
extern "C" int printf(const char *, ...);
struct V { int v; V() : v(0) {} virtual int f() { return 1; } };
struct W : virtual V { int w; W(); virtual int k() { return 7; } };
struct D : virtual W { int d; D(); };
W::W() : w(4) { printf("W:%d,%d ", f(), v); v = 5; }
D::D() : d(9) { printf("D:%d,%d ", f(), v); }
int main() {
    D dd; V *pv = &dd; W *pw = &dd;
    printf("| %d %d %d %d\n", pv->f(), pw->f(), pw->k(), pv->v);
    return 0;
}
