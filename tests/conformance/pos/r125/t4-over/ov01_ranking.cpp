// [over.ics.rank]: exact match > promotion > conversion; char->int promotion vs char->short conversion; float->double promotion; bool; enum promotion; pointer conversions to void*/base.
#include <cstdio>
const char *f(int) { return "int"; }
const char *f(short) { return "short"; }
const char *f(double) { return "double"; }
const char *f(long) { return "long"; }
const char *g(double) { return "double"; }
const char *g(long double) { return "long double"; }
const char *h(int) { return "int"; }
const char *h(unsigned) { return "unsigned"; }
const char *k(void *) { return "void*"; }
const char *k(const void *) { return "const void*"; }
struct B {}; struct D : B {};
const char *m(B *) { return "B*"; }
const char *m(void *) { return "void*"; }
const char *n(const B &) { return "const B&"; }
const char *n(B &) { return "B&"; }
enum E { E0 };
const char *q(int) { return "int"; }
const char *q(E) { return "E"; }
const char *r(int) { return "int"; }
const char *r(bool) { return "bool"; }
const char *s(long long) { return "long long"; }
const char *s(unsigned) { return "unsigned"; }
int main() {
    char c = 'a'; float fl = 1.0f; unsigned char uc = 1; bool b = true; short sh = 2;
    std::printf("%s %s %s %s %s\n", f(c), f(fl), f(uc), f(b), f(sh));
    std::printf("%s %s\n", g(fl), g(1.0L));
    std::printf("%s %s %s\n", h(c), h(1u), h(sh));
    int *ip = 0; const int *cip = 0;
    std::printf("%s %s\n", k(ip), k(cip));
    D d; const D cd = D(); std::printf("%s %s %s\n", m(&d), n(d), n(cd));
    std::printf("%s %s %s\n", q(E0), q(0), q(1));
    std::printf("%s %s %s\n", r(1), r(true), r(ip == 0));
    std::printf("%s %s\n", s(1u), s(1LL));
    std::printf("%s\n", f('a' + 1));
    return 0;
}
