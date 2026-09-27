// An array of a class with constructors, from a braced list: [dcl.init.aggr]/2. Each element is
// copy-initialised from its initialiser - `S(a)` built in place, `{a, b}`, `{}`, a converting
// constructor, a copy - and the elements the list leaves out are value-initialised.
#include <cstdio>
#include "lifetime.h"

struct S {
    int v;
    S() : v(-1) { lfBuilt(this, "S"); }
    S(int a) : v(a) { lfBuilt(this, "S"); }
    S(int a, int b) : v(a * 10 + b) { lfBuilt(this, "S"); }
    S(const S &o) : v(o.v + 100) { lfBuilt(this, "S"); }
    ~S() { lfGone(this, "S"); }
};

int main()
{
    lfWatch();
    S named(7);
    {
        S a[6] = { S(1), {2, 3}, {}, 4, named };            // the sixth by the default constructor
        std::printf("a: %d %d %d %d %d %d\n", a[0].v, a[1].v, a[2].v, a[3].v, a[4].v, a[5].v);
    }
    {
        S b[4] = { S(5) };                                  // three left to the loop
        std::printf("b: %d %d %d %d\n", b[0].v, b[1].v, b[2].v, b[3].v);
    }
    {
        S g[2][3] = { { S(1), S(2) }, { S(4), S(5), S(6) } };   // a nested list per row
        S h[2][2] = { S(1), S(2), S(3) };                       // the braces elided
        std::printf("g: %d %d %d / %d %d %d\n", g[0][0].v, g[0][1].v, g[0][2].v, g[1][0].v, g[1][1].v, g[1][2].v);
        std::printf("h: %d %d / %d %d\n", h[0][0].v, h[0][1].v, h[1][0].v, h[1][1].v);
    }
    S c[2] = { S(8), S(9) };
    std::printf("c: %d %d\n", c[0].v, c[1].v);
    return 0;
}
