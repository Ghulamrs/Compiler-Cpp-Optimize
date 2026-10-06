// [conv.ptr]/3 with [class.access.base]/4: Derived * converts to Base * wherever the base is
// accessible - inside the class and its friends for a private base, inside a class derived
// from it for a protected one - and the pointer moves to the base wherever that base sits.
extern "C" int printf(const char *, ...);

struct A { virtual ~A() {} int a; A() : a(1) {} };
struct Pad { int p; Pad() : p(9) {} };

static int takes(A *p) { return p->a; }
static int takesRef(const A &r) { return r.a; }

struct Hidden : private A {
    Hidden() { a = 5; }
    A *self() { return this; }                        // return
    int viaInit() { A *p = this; return p->a; }       // initialisation
    int viaArg() { return takes(this); }              // an argument
    int viaAssign() { A *p = 0; p = this; return p->a; }
    int viaRef() { A &r = *this; return r.a; }        // a reference
    int viaConstRef() { return takesRef(*this); }
    friend int peek(Hidden &h);
};
int peek(Hidden &h) { A *p = &h; return p->a + 100; }  // a friend

// A private base that is not the first: the pointer has to move to it.
struct B { int b; };
struct Second : Pad, private B {
    Second() { b = 7; }
    B *self() { return this; }
    int moved() { return (char *)self() - (char *)this != 0; }
};

// A protected base is reached from a class derived from the one that names it.
struct Shielded : protected A { Shielded() { a = 3; } };
struct Below : Shielded {
    A *up() { return this; }
    int viaRef() { A &r = *this; return r.a + 10; }
    int member() { return a + 20; }                   // and its members, as Below's
};

int main() {
    Hidden h;
    printf("%d %d %d %d %d %d %d\n", h.self()->a, h.viaInit(), h.viaArg(), h.viaAssign(),
           h.viaRef(), h.viaConstRef(), peek(h));
    Second s;
    printf("%d %d\n", s.self()->b, s.moved());
    Below w;
    printf("%d %d %d\n", w.up()->a, w.viaRef(), w.member());
    // A cast ignores access, [expr.cast]/4, and still finds where the base sits.
    printf("%d %d\n", ((B *)&s)->b, ((A *)&h)->a);
    return 0;
}
