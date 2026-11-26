#include <utility>
#include <cstdio>
template <class K> struct Base {
    int n;
    Base() : n(1) {}
    Base(const Base &o) : n(o.n + 10) {}
    Base(Base &&o) : n(o.n + 100) { o.n = 0; }
};
template <class K> struct Der : Base<K> {
    Der() {}
    Der(const Der &o) : Base<K>(o) {}
    Der(Der &&o) : Base<K>(std::move(o)) {}
};
struct B2 { int n; B2() : n(2) {} B2(B2 &&o) : n(o.n + 100) {} B2(const B2 &o) : n(o.n + 10) {} };
struct D2 : B2 { D2() {} D2(D2 &&o) : B2(std::move(o)) {} };
template <class T> struct Bits {
    unsigned v;
    class reference {
        friend struct Bits;
        Bits *b; int pos;
        reference(Bits &bb, int p) : b(&bb), pos(p) {}
    public:
        reference &operator=(bool x) { if (x) b->v |= 1u << pos; else b->v &= ~(1u << pos); return *this; }
        reference &operator=(const reference &x) { return *this = (bool)x; }
        operator bool() const { return (b->v >> pos) & 1; }
    };
    Bits() : v(0) {}
    reference operator[](int p) { return reference(*this, p); }
};
int main() {
    Der<int> a;
    Der<int> b(std::move(a));
    D2 c;
    D2 d(std::move(c));
    Bits<int> bits;
    bits[3] = true;
    bits[1] = bits[3];
    std::printf("%d %d %d %d %u\n", a.n, b.n, c.n, d.n, bits.v);
}
