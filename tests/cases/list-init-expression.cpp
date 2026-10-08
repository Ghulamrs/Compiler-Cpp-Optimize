// `T{...}` as an expression - [expr.type.conv]/2 makes it a temporary that is
// list-initialised: a class with constructors through one of them, an aggregate
// by its members, a scalar from its one element. Under the ledger.
#include "lifetime.h"

struct P {
    int a, b;
    P(int x, int y) : a(x), b(y) { lfBuilt(this, "P"); }
    P(const P &o) : a(o.a), b(o.b) { lfBuilt(this, "P"); }
    ~P() { lfGone(this, "P"); }
};

struct Agg { int x; int y; };

static int sum(const P &p) { return p.a + p.b; }
static int first(Agg a) { return a.x; }
static Agg makeAgg(int k) { return Agg{k, k * 2}; }

int main() {
    lfWatch();
    {
        int s = sum(P{1, 2});
        printf("argument %d\n", s);
    }
    {
        // Bound to a reference rather than copied: a copy from a prvalue is clang's to elide.
        const P &p = P{3, 4};
        int b = P{5, 6}.b;
        printf("bound %d %d member %d\n", p.a, p.b, b);
    }
    {
        // A returned temporary is kept off the ledger: a by-value return moves the bytes.
        printf("returned %d %d\n", makeAgg(7).x, makeAgg(7).y);
    }
    {
        int f = first(Agg{8, 9});
        int y = Agg{10, 11}.y;
        Agg z = Agg{12, 13};
        printf("aggregate %d %d %d %d\n", f, y, z.x, z.y);
    }
    {
        int i = int{14};
        double d = double{15};
        long l = long{16};
        printf("scalar %d %g %ld\n", i, d, l);
    }
    return 0;
}
