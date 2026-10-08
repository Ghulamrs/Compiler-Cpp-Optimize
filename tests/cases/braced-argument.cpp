// A braced list written as a call's argument copy-list-initialises the
// parameter - [dcl.init]/16 and [over.match.list]: a class with constructors
// through one of them, an aggregate by its members, a scalar from one element,
// a reference to a temporary so made. The ledger watches the reference forms;
// a by-value parameter is built from the list directly by clang and through a
// temporary here, a copy C++11 lets either elide, so that class is off it.
#include "lifetime.h"

struct P {
    int a, b;
    P(int x, int y) : a(x), b(y) { lfBuilt(this, "P"); }
    P(const P &o) : a(o.a), b(o.b) { lfBuilt(this, "P"); }
    ~P() { lfGone(this, "P"); }
};

struct Q {
    int a, b;
    Q(int x, int y) : a(x), b(y) {}
};

struct Agg { int x; int y; };

static int byValue(Q q) { return q.a * 10 + q.b; }
static int byRef(const P &p) { return p.a * 10 + p.b; }
static int agg(Agg a) { return a.x * 10 + a.y; }
static int aggRef(const Agg &a) { return a.x * 10 + a.y; }
static int scalar(int n) { return n; }
static double real(double d) { return d; }

struct Box {
    int k;
    Box(int n) : k(n) { lfBuilt(this, "Box"); }
    ~Box() { lfGone(this, "Box"); }
    int add(Q q) const { return k + q.a + q.b; }
    int addRef(const P &p) const { return k + p.a + p.b; }
};

int main() {
    lfWatch();
    {
        int v = byValue({1, 2});
        int r = byRef({3, 4});
        printf("class %d %d\n", v, r);
    }
    {
        int v = agg({5, 6});
        int r = aggRef({7, 8});
        printf("aggregate %d %d\n", v, r);
    }
    {
        int s = scalar({9});
        double d = real({10});
        printf("scalar %d %g\n", s, d);
    }
    {
        Box b{100};
        int v = b.add({1, 2});
        int r = b.addRef({3, 4});
        printf("member %d %d\n", v, r);
    }
    return 0;
}
