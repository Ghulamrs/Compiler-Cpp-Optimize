// Live-object counting (not construction counting, which may legally differ): every object constructed is destroyed exactly once; returns by value; pass by value; temporaries in expressions; arrays of objects.
#include <cstdio>
struct Live { static int alive; static int peak; int v; Live(int v = 0) : v(v) { bump(); } Live(const Live &o) : v(o.v) { bump(); } ~Live() { --alive; } static void bump() { if (++alive > peak) peak = alive; } };
int Live::alive = 0; int Live::peak = 0;
Live make(int v) { Live l(v); return l; }
Live pass(Live l) { l.v++; return l; }
Live chain(int n) { return n ? pass(make(n)) : Live(); }
struct Holder { Live a, b; Holder() : a(1), b(2) {} };
int main() {
    { Live a = make(1); Live b = pass(a); Live c = chain(3); std::printf("%d %d %d %d\n", a.v, b.v, c.v, Live::alive > 0); }
    std::printf("%d\n", Live::alive);
    { Live arr[3]; Holder h; Live *p = new Live[2]; delete[] p; std::printf("%d\n", Live::alive); }
    std::printf("%d\n", Live::alive);
    { Live t = Live(Live(Live(9))); std::printf("%d\n", t.v); }
    std::printf("%d %d\n", Live::alive, Live::peak <= 6);
    return 0;
}
