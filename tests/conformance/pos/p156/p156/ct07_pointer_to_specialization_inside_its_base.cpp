#include <cstdio>

// [temp.inst]/1: a pointer to a specialization does not need it complete. B<char>'s pointer to D<char> is
// read while B<char> is being made for D<char>'s own base, so D<char> cannot be made then.
template <class C> struct B;
template <class C> struct D;
template <class C> struct E;
typedef D<char> DC;
typedef E<char> EC;

template <class C> struct B {
    D<C> *tied;
    int x;
    B() : tied(0), x(1) {}
    D<C> *tie() const { return tied; }
};
template <class C> struct D : virtual B<C> { int y; D() : y(2) {} };
template <class C> struct E : virtual B<C> { int z; E() : z(3) {} };

int main() {
    EC e;
    DC d;
    e.tied = &d;
    std::printf("%d %d %d\n", e.x + e.z, e.tie()->y, d.tie() == 0);
    return 0;
}
