// `= default` - [dcl.fct.def.default]: a special member defaulted on its first
// declaration inside the class is the one the compiler would have written,
// trivial where that one is trivial and synthesised where it has work. Measured
// against clang: a defaulted copy runs the members' copy constructors, a
// defaulted default constructor beside a written one gives `C c;` a function to
// call, a defaulted virtual destructor takes its slot, and a defaulted move
// constructor copies a member that has only a copy; a defaulted move assignment
// moves a member that has a move assignment.
extern "C" int printf(const char *, ...);

struct Tr {
    int n;
    Tr() : n(0) {}
    Tr(const Tr &o) : n(o.n + 1) {}
    Tr &operator=(const Tr &o) { n = o.n + 10; return *this; }
};

struct A { int v; A() = default; };

struct B {
    Tr t;
    int k;
    B() = default;
    B(const B &) = default;
    B &operator=(const B &) = default;
    ~B() = default;
};

struct C { C(int x) : v(x) {} C() = default; int v; };

struct VB { virtual ~VB() = default; virtual int f() { return 1; } };
struct VD : VB { int f() override { return 2; } };

struct M { Tr t; int k; M() = default; M(M &&) = default; };

// A defaulted move assignment moves each member that has a move of its own.
struct Mv { int n; Mv() : n(0) {} Mv &operator=(const Mv &o) { n = o.n + 10; return *this; }
            Mv &operator=(Mv &&o) { n = o.n + 100; return *this; } };
struct MA { Mv t; int k; MA() = default; MA &operator=(MA &&) = default; };

int main() {
    A a; a.v = 3;
    printf("%d\n", a.v);

    B b; b.k = 5;
    B c = b;
    printf("%d %d\n", c.t.n, c.k);
    c = b;
    printf("%d %d\n", c.t.n, c.k);

    C c0; c0.v = 7;
    C c1(4);
    printf("%d %d\n", c0.v, c1.v);

    VB vb;
    int r0 = vb.f();
    printf("%d\n", r0);
    VD vd;
    VB *p = &vd;
    int r = p->f();
    printf("%d\n", r);

    M m1; m1.k = 9;
    M m2(static_cast<M &&>(m1));
    printf("%d %d\n", m2.t.n, m2.k);

    MA ma, mb; mb.k = 3;
    ma = static_cast<MA &&>(mb);
    printf("%d %d\n", ma.t.n, ma.k);
    return 0;
}
