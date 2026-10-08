// `new T{...}` - [expr.new]/15 list-initialises the object made: a class
// with constructors through one, an aggregate by its members, a scalar from
// the one element. Under the ledger, every object deleted once.
#include "lifetime.h"

struct P {
    int a, b;
    P(int x, int y) : a(x), b(y) { lfBuilt(this, "P"); }
    ~P() { lfGone(this, "P"); }
};

struct Agg { int x; int y; };

int main() {
    lfWatch();
    P *p = new P{1, 2};
    printf("class %d %d\n", p->a, p->b);
    delete p;

    Agg *a = new Agg{3, 4};
    printf("aggregate %d %d\n", a->x, a->y);
    delete a;

    int *i = new int{5};
    double *d = new double{6};
    printf("scalar %d %g\n", *i, *d);
    delete i;
    delete d;

    const P *c = new const P{7, 8};
    printf("const %d %d\n", c->a, c->b);
    delete c;
    return 0;
}
