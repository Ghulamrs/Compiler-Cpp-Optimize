// An array of a class with a constructor, its length taken from a braced list of
// temporaries - `const Row rows[] = { Row("a"), Row("b") };` - aborted the compiler with
// std::length_error: the list path read a length of -1. Each element is copy-initialised
// from its initialiser, the length given or deduced, local, static local and file scope.
#include "lifetime.h"

// The report registered before any static array, so it reads after they are destroyed.
struct Watcher { Watcher(); };
Watcher::Watcher() { lfWatch(); }
static Watcher watcher;

class Row {
public:
    Row(const char *n);
    const char *n_;
};
Row::Row(const char *n) : n_(n) {}

// a destructor, so the ledger can count every element built and gone
struct Owned {
    Owned(int v = 0);
    ~Owned();
    int v;
};
Owned::Owned(int v) : v(v) { lfBuilt(this, "Owned"); }
Owned::~Owned() { lfGone(this, "Owned"); }

// a written copy constructor, so copy-initialisation from a temporary has one to reach
struct Copied {
    Copied(int v);
    Copied(const Copied &o);
    ~Copied();
    int v;
};
Copied::Copied(int v) : v(v) { lfBuilt(this, "Copied"); }
Copied::Copied(const Copied &o) : v(o.v + 100) { lfBuilt(this, "Copied"); }
Copied::~Copied() { lfGone(this, "Copied"); }

const Row fileRows[] = { Row("x"), Row("y"), Row("z") };
Owned fileOwned[] = { Owned(7), 8 };

static int sumStatic() {
    static const Owned held[] = { Owned(1), Owned(2), Owned(3) };
    int s = 0;
    for (int i = 0; i < 3; i++) s += held[i].v;
    return s;
}

int main() {
    const Row rows[] = { Row("a"), Row("b") };
    Row more[] = { Row("c"), "d", Row("e") };
    Row fixed[2] = { Row("f"), Row("g") };
    printf("%d %d %d\n", (int)(sizeof rows / sizeof rows[0]),
           (int)(sizeof more / sizeof more[0]), (int)(sizeof fixed / sizeof fixed[0]));
    printf("%s%s %s%s%s %s%s\n", rows[0].n_, rows[1].n_, more[0].n_, more[1].n_, more[2].n_,
           fixed[0].n_, fixed[1].n_);
    printf("%d %s%s%s\n", (int)(sizeof fileRows / sizeof fileRows[0]), fileRows[0].n_,
           fileRows[1].n_, fileRows[2].n_);
    {
        const Owned o[] = { Owned(4), Owned(5) };
        Owned p[3] = { Owned(6) , 9 };
        printf("%d %d %d %d %d\n", o[0].v, o[1].v, p[0].v, p[1].v, p[2].v);
    }
    {
        // nested lists for a row each, the outer length deduced
        Owned grid[][2] = { { Owned(10), Owned(11) }, { Owned(12), 13 }, { 14 } };
        printf("%d %d %d\n", (int)(sizeof grid / sizeof grid[0]), grid[1][1].v, grid[2][0].v);
    }
    {
        Copied c[] = { Copied(1), Copied(2) };
        // a copy from a temporary may be elided, so the value says only that it is one of the two
        printf("%d %d\n", c[0].v % 100, c[1].v % 100);
    }
    printf("%d %d %d\n", sumStatic(), sumStatic(), fileOwned[0].v + fileOwned[1].v);
    return 0;
}
