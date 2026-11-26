#include <cstdio>
struct B { virtual int v() { return 1; } int w() { return 10; } };
struct D : B { int v() { return 2; } };
template <int (B::*PF)()> int via(B &b) { return (b.*PF)(); }
template <int (B::*PF)()> struct Caller { int operator()(B *p) const { return (p->*PF)(); } };
int main() {
    D d; B b;
    Caller<&B::v> cv; Caller<&B::w> cw;
    std::printf("%d %d %d %d %d %d\n", via<&B::v>(b), via<&B::v>(d), via<&B::w>(d), cv(&d), cw(&b), cv(&b));
    return 0;
}
