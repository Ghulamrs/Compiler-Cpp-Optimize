// Direct- and copy-list-initialisation of a class with constructors -
// [dcl.init.list]/3 and [over.match.list]: an initializer_list constructor is
// tried first, then every constructor with the braces' elements as arguments.
// Under the ledger: every object built is destroyed exactly once.
#include "lifetime.h"

struct P {
    int a, b;
    P(int x, int y) : a(x), b(y) { lfBuilt(this, "P"); }
    P(const P &o) : a(o.a), b(o.b) { lfBuilt(this, "P"); }
    ~P() { lfGone(this, "P"); }
};

struct E {
    int v;
    explicit E(int x) : v(x) { lfBuilt(this, "E"); }
    ~E() { lfGone(this, "E"); }
};

// An initializer_list constructor beside an ordinary one: the braces reach
// the list, the parentheses reach the pair.
#include <initializer_list>
struct L {
    int n, first;
    L(std::initializer_list<int> xs) : n(0), first(0) {
        for (const int *p = xs.begin(); p != xs.end(); ++p) { n++; if (n == 1) first = *p; }
        lfBuilt(this, "L");
    }
    L(int x, int y) : n(-1), first(x + y) { lfBuilt(this, "L"); }
    ~L() { lfGone(this, "L"); }
};

// A member initialiser beside a written constructor: C++11 takes the braces
// to the constructor, the member initialiser filling what the list leaves.
struct N {
    int k = 7;
    int m;
    N(int x) : m(x) { lfBuilt(this, "N"); }
    ~N() { lfGone(this, "N"); }
};

// A class with no constructor at all is an aggregate, and so is a scalar an
// element: the same braces, by [dcl.init.list]/3's other bullets.
struct Agg { int x; double y; };

// Off the ledger: a global built before main is destroyed after the report prints.
struct G { int a, b; G(int x, int y) : a(x), b(y) {} };
static G global{8, 9};
static Agg gagg{5, 6.5};

int main() {
    lfWatch();
    {
        P p{1, 2};
        P q = {3, 4};
        P c{p};
        printf("direct %d %d copy %d %d from %d %d\n", p.a, p.b, q.a, q.b, c.a, c.b);
    }
    {
        E e{5};
        printf("explicit %d\n", e.v);
    }
    {
        L one{10, 20, 30};
        L two(10, 20);
        L three = {40};
        printf("list %d %d pair %d %d one %d %d\n", one.n, one.first, two.n, two.first, three.n, three.first);
    }
    {
        N n{11};
        N o = {12};
        printf("member-init %d %d %d %d\n", n.k, n.m, o.k, o.m);
    }
    {
        Agg a{1, 2.5};
        int i{6};
        double d{7};
        Agg b = {3, 4.5};
        printf("aggregate %d %g %d %g scalar %d %g\n", a.x, a.y, b.x, b.y, i, d);
    }
    {
        static P s{13, 14};
        static Agg sagg = {15, 16.5};
        printf("static %d %d %d %g global %d %d %d %g\n", s.a, s.b, sagg.x, sagg.y,
               global.a, global.b, gagg.x, gagg.y);
    }
    return 0;
}
