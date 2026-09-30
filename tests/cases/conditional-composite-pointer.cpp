// [expr.cond]/6: two pointer arms meet at their composite pointer type,
// [expr]/13 - the qualification union at every level, a base beside a
// derived class, void * beside an object pointer, and a null constant.
extern "C" int printf(const char *, ...);

// Constructors out of line: written inline, clang emits only C2 on Linux.
struct Base { int b; Base(); };
struct Other { int o; Other(); };
struct Derived : Other, Base { int d; Derived(); };
Base::Base() : b(1) {}
Other::Other() : o(7) {}
Derived::Derived() : d(2) {}

template <class C> struct Buf {
    C *heap_;
    C small_[16];
    const C *p() const { return heap_ != 0 ? heap_ : small_; }
};

static int pick(bool c) {
    int x = 10;
    const int y = 20;
    int *px = &x;
    const int *py = &y;
    return *(c ? px : py);
}

int main() {
    Buf<char> s;
    s.heap_ = 0;
    s.small_[0] = 'x';
    s.small_[1] = 0;
    printf("%s\n", s.p());

    int x = 3;
    const int y = 4;
    int *const pc = &x;
    const int *pk = &y;
    printf("%d %d\n", *(true ? pc : pk), *(false ? pc : pk));
    printf("%d %d\n", pick(true), pick(false));

    int *px = &x;
    int **ppx = &px;
    const int *const cq = &y;
    const int *const *ppc = &cq;
    printf("%d %d\n", **(true ? ppx : ppc), **(false ? ppx : ppc));

    Derived d;
    Base bb;
    Other oo;
    Base *pb = &bb;
    Derived *pd = &d;
    Base *r1 = true ? pd : pb;
    Base *r2 = false ? pb : pd;
    printf("%d %d %d %d\n", oo.o, r1->b, r2->b, (true ? pd : pb) == static_cast<Base *>(pd));
    const Base *r3 = true ? pd : static_cast<const Base *>(pb);
    printf("%d\n", r3->b);

    void *pv = &x;
    const void *r4 = false ? pk : pv;
    printf("%d\n", r4 == static_cast<const void *>(&x));

    int *pn = true ? 0 : px;
    printf("%d\n", pn == 0);
    return 0;
}
