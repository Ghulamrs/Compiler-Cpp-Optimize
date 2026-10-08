// The move constructor the compiler writes.
//
// [class.copy]/9 gives a class one only if it declared none of a copy
// constructor, a move constructor, a copy or move assignment, or a
// destructor - each of those being evidence that the class manages something
// by hand, and a memberwise move of such a class is how a double free
// happens. Two of the five cannot be written in this language yet, so what is
// tested here is the other three.
//
// [class.copy]/15 says the body moves each base and each member. A subobject
// with no move constructor is *copied*, which is not a shortcut: `T &&` binds
// to `const T &`, so moving something that has only a copy is what its copy
// constructor does.
//
// The constructors are defined out of line deliberately. Defined inside the
// class body they are inline, and clang then emits only the C2 variants on
// x86_64-linux while emitting both on Darwin - see move-constructor.nonames,
// where a case that does it the other way records exactly that.
extern "C" int printf(const char *, ...);

struct Loud {
    int v;
    Loud();
    Loud(const Loud &o);
    Loud(Loud &&o);
};
Loud::Loud() { v = 0; }
Loud::Loud(const Loud &o) { v = o.v; printf("  copy %d\n", v); }
Loud::Loud(Loud &&o) { v = o.v; o.v = -1; printf("  move %d\n", v); }

struct CopyOnly {
    int v;
    CopyOnly();
    CopyOnly(const CopyOnly &o);
};
CopyOnly::CopyOnly() { v = 0; }
CopyOnly::CopyOnly(const CopyOnly &o) { v = o.v; printf("  copyonly %d\n", v); }

struct Plain { Loud a; int n; };
struct Mixed { CopyOnly c; Loud l; };
struct HasDtor { Loud a; ~HasDtor(); };
HasDtor::~HasDtor() { printf("  ~HasDtor\n"); }
struct Base { Loud b; };
struct Derived : Base { Loud d; };
struct Arr { Loud e[2]; };

int byValue(Plain q) { return q.a.v + q.n; }

int main() {
    printf("Plain:\n");
    Plain p; p.a.v = 1; p.n = 5;
    Plain p2(static_cast<Plain &&>(p));
    printf("  p.a=%d p2.a=%d p2.n=%d\n", p.a.v, p2.a.v, p2.n);

    printf("Mixed:\n");
    Mixed m; m.c.v = 2; m.l.v = 3;
    Mixed m2(static_cast<Mixed &&>(m));
    printf("  m.c=%d m.l=%d m2.c=%d m2.l=%d\n", m.c.v, m.l.v, m2.c.v, m2.l.v);

    printf("HasDtor:\n");
    {
        HasDtor h; h.a.v = 4;
        HasDtor h2(static_cast<HasDtor &&>(h));
        printf("  h.a=%d h2.a=%d\n", h.a.v, h2.a.v);
    }

    // Base as a complete object as well as a base subobject. Without this it
    // is only ever built as part of a Derived, and then clang has no use for
    // its C1 - the complete-object constructor - and does not emit one.
    printf("Base:\n");
    Base bb; bb.b.v = 20;
    Base bb2(static_cast<Base &&>(bb));
    printf("  bb.b=%d bb2.b=%d\n", bb.b.v, bb2.b.v);

    printf("Derived:\n");
    Derived d; d.b.v = 6; d.d.v = 7;
    Derived d2(static_cast<Derived &&>(d));
    printf("  d.b=%d d.d=%d d2.b=%d d2.d=%d\n", d.b.v, d.d.v, d2.b.v, d2.d.v);

    printf("Arr:\n");
    Arr r; r.e[0].v = 8; r.e[1].v = 9;
    Arr r2(static_cast<Arr &&>(r));
    printf("  r=%d,%d r2=%d,%d\n", r.e[0].v, r.e[1].v, r2.e[0].v, r2.e[1].v);

    printf("byValue:\n");
    Plain w; w.a.v = 10; w.n = 1;
    int got = byValue(static_cast<Plain &&>(w));
    printf("  got %d, w.a=%d\n", got, w.a.v);
    return 0;
}
