// A const member named in a mem-initialiser list - [class.base.init]/8: the
// store from its own constructor's list is the member's initialisation, not an
// assignment, so the const says nothing against it. Refused by name until now.
extern "C" int printf(const char *, ...);

struct Inner {
    int v;
    Inner(int a);
};

struct S {
    const int k;
    const double d;
    const char *const name;
    const Inner in;
    const int zeroed;
    const int fromClass = 7;
    S(int a, const char *n);
    S(int a, const char *n, int c);
};

Inner::Inner(int a) : v(a * 10) {}
S::S(int a, const char *n) : k(a), d(a / 2.0), name(n), in(a), zeroed() {}
S::S(int a, const char *n, int c) : k(a), d(0.5), name(n), in(a + 1), zeroed(c), fromClass(c * 2) {}

struct Base {
    const int b;
    Base(int x);
};

struct Derived : Base {
    const long big;
    Derived(int x);
};

Base::Base(int x) : b(x) {}
Derived::Derived(int x) : Base(x), big(x * 100000L) {}

int main(void) {
    S s(3, "three");
    printf("%d %.1f %s %d %d %d\n", s.k, s.d, s.name, s.in.v, s.zeroed, s.fromClass);
    S t(4, "four", 9);
    printf("%d %.1f %s %d %d %d\n", t.k, t.d, t.name, t.in.v, t.zeroed, t.fromClass);
    Base bb(2);
    printf("%d\n", bb.b);
    Derived dd(6);
    printf("%d %ld\n", dd.b, dd.big);
    const int *p = &s.k;
    printf("%d\n", *p);
    return 0;
}
