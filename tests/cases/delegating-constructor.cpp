// A delegating constructor - [class.base.init]/6: a mem-initialiser naming the
// class itself makes that the list's only entry, the target constructor (chosen
// by overload resolution, defaults applied) builds the bases, the members and
// the vptr, and the delegating body runs after the target's. Every object is
// built once and destroyed once - the ledger line says so. Refused until now.
#include "lifetime.h"

struct Part {
    int v;
    Part(int x);
    ~Part();
};
Part::Part(int x) : v(x) { lfBuilt(this, "Part"); }
Part::~Part() { lfGone(this, "Part"); }

struct Base {
    int b;
    Base(int x);
};
Base::Base(int x) : b(x) { printf("Base(%d) ", x); }

struct S : Base {
    Part p;
    int n;
    const char *how;
    S(int x, const char *h, int scale = 10);
    S(int x);
    S();
    virtual int kind() const;
    virtual ~S();
};
S::S(int x, const char *h, int scale) : Base(x + 1), p(x * scale), n(x), how(h) {
    lfBuilt(this, "S");
    printf("target(%d) ", kind());
}
S::S(int x) : S(x, "int") { printf("int(%d) ", kind()); }
S::S() : S(7) { printf("default "); }
int S::kind() const { return 1; }
S::~S() { lfGone(this, "S"); }

namespace geo {
    struct Pt {
        int x, y;
        Pt(int a, int b);
        Pt(int a);
    };
    Pt::Pt(int a, int b) : x(a), y(b) {}
    Pt::Pt(int a) : Pt(a, a * 2) {}
}

template <class T> struct Box {
    T held;
    int tag;
    Box(T v, int t) : held(v), tag(t) {}
    Box(T v) : Box(v, 99) {}
};

int main(void) {
    lfWatch();
    {
        S a;
        printf("| %d %d %d %s\n", a.b, a.p.v, a.n, a.how);
        S b(3);
        printf("| %d %d %d %s\n", b.b, b.p.v, b.n, b.how);
    }
    geo::Pt q(5);
    printf("%d %d\n", q.x, q.y);
    Box<double> bx(1.5);
    printf("%.1f %d\n", bx.held, bx.tag);
    return 0;
}
