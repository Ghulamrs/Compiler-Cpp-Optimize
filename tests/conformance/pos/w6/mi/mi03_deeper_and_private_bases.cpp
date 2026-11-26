// [class.mi], [class.access.base]: tables of a base's own bases, an override reached through a private base inside the class
#include <cstdio>
struct I1 { virtual int a() { return 1; } virtual ~I1() {} };
struct J1 { virtual int j1() { return 6; } };
struct J2 { virtual int j2() { return 7; } };
struct M : J1, J2 { int j2() { return 8; } };
struct X2 : I1, private M {
    int a() { return 12; }
    int j1() { return 9; }
    int viaBases() { J1 *p = this; J2 *q = this; return p->j1() * 100 + q->j2(); }
};
struct C : X2 { int a() { return 13; } int j1() { return 14; } };
int main() {
    X2 x; C c; I1 *pc = &c;
    std::printf("%d %d %d %d\n", x.a(), x.viaBases(), pc->a(), c.viaBases());
    return 0;
}
