// [class.copy]/28: an empty class's implicit assignment assigns no member, so through an empty base subobject
// it leaves the member sharing its address alone.
#include <cstdio>
struct Empty {};
struct E2 : Empty {};
struct H : private Empty { unsigned long long v; Empty &e() { return *this; } };
struct H2 : private E2 { unsigned char c[7]; unsigned long long v; E2 &e() { return *this; } };
struct M { Empty x; char c; };
int main() {
    H a, b; a.v = 0x1122334455667788ULL; b.v = 0xAAAAAAAAAAAAAAAAULL;
    a.e() = b.e(); Empty x; a.e() = x;
    H2 p, q; p.v = 7; p.c[0] = 9; E2 t; p.e() = t; p.e() = q.e();
    M m, n; m.c = 5; n.c = 6; m.x = n.x;
    Empty &r = (a.e() = b.e());
    std::printf("%llx %d %d %d %d %d\n", a.v, (int)p.v, (int)p.c[0], (int)m.c, (int)(&r == &a.e()), (int)sizeof(H));
    m = n;
    std::printf("%d\n", (int)m.c);
    return 0;
}
