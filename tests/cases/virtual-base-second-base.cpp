// **A second base with a virtual base of its own.** `Two : E1, E2` where E1
// names V virtually and E2 names W: reading W through an `E2 *` walks E2's
// own vptr for a `vbase_offset`, which lives in the secondary table Two's
// group lays down for E2 - clang gives 8, and cxx1 gave 7 (V's member) until
// the Itanium group learned to write a secondary table for a base that is
// dynamic only because it has a virtual base (2026-09-15, the virtual
// inheritance round). x86_64-windows had it right a round earlier, each
// subobject holding a vbptr getting its own vbtable there, measured with cl.
extern "C" int printf(const char *, ...);

struct V { int a; };
struct W { int w; };
struct E1 : virtual V { int e; };
struct E2 : virtual W { int f; };
struct Two : E1, E2 { int t; };

int main() {
    Two y;
    y.a = 7; y.w = 8; y.e = 9;
    E2 *q = &y;
    E1 *r = &y;
    printf("%d %d %d %d %d\n", y.a, y.w, y.e, q->w, r->a);
    return 0;
}
