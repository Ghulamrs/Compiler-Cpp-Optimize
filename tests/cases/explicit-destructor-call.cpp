// The explicit destructor call, [class.dtor]/14, and the pseudo-destructor,
// [expr.pseudo] - what a container writes to take an element apart in
// storage it owns, and what generic code writes so that `T` may be `int`.
//
// Five spellings: `p->~S()`, `(*p).~S()`, `p->S::~S()`, a virtual destructor
// called through a base pointer and dispatching to the most derived one, and
// `p->~T()` where T names a scalar, which evaluates `p` and does nothing else.
// The objects are built with placement new into storage of their own, so the
// ledger sees each destroyed exactly once and nothing destroyed twice; a
// qualified call on a derived object runs the base's destructor alone and is
// kept on a class the ledger does not watch, since that shape leaves the
// derived part alive by design.
//
// Special members are defined out of line, so the names suite compares
// linkage names rather than which C1/C2 clang chose to emit inline.
#include "lifetime.h"
#include <new>

struct S {
    int v;
    S(int n);
    ~S();
};
S::S(int n) : v(n) { lfBuilt(this, "S"); }
S::~S() { printf("~S %d\n", v); lfGone(this, "S"); }

struct Base {
    int b;
    Base();
    virtual ~Base();
};
Base::Base() : b(1) { lfBuilt(this, "Base"); }
Base::~Base() { printf("~Base\n"); lfGone(this, "Base"); }

struct Derived : Base {
    int d;
    Derived();
    ~Derived();
};
Derived::Derived() : d(2) { lfBuilt(this, "Derived"); }
Derived::~Derived() { printf("~Derived\n"); lfGone(this, "Derived"); }

struct Plain { int a; };

// The qualified call on a derived object, traced and not ledgered.
struct TB { virtual ~TB() { printf("~TB\n"); } };
struct TD : TB { ~TD() { printf("~TD\n"); } };

template <class T> void destroy(T *p) { p->~T(); }

typedef int Int;

struct Self {
    int k;
    Self(int n) : k(n) { lfBuilt(this, "Self"); }
    ~Self() { printf("~Self %d\n", k); lfGone(this, "Self"); }
    void kill() { this->~Self(); }
};

static unsigned char buf[64];

int main() {
    lfWatch();
    S *p = new (buf) S(1);
    p->~S();
    p = new (buf) S(2);
    (*p).~S();
    p = new (buf) S(3);
    p->S::~S();
    const S *cp = new (buf) S(4);
    cp->~S();
    destroy(new (buf) S(5));

    Base *bp = new (buf) Derived();
    printf("%d %d\n", bp->b, static_cast<Derived *>(bp)->d);
    bp->~Base();

    TB *tp = new (buf) TD();
    tp->TB::~TB();
    printf("qualified done\n");

    Plain *pp = new (buf) Plain();
    pp->a = 9;
    pp->~Plain();
    printf("plain %d\n", pp->a);

    int n = 7;
    int *ip = &n;
    ip->~Int();
    n.~Int();
    destroy(ip);
    printf("int %d\n", n);

    Self *sp = new (buf) Self(6);
    sp->kill();
    printf("end\n");
    return 0;
}
