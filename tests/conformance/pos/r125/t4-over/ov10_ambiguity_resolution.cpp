// Programs that are NOT ambiguous though they look it: const overload on this, int vs long with short arg, exact match through typedef, conversions with cv-qualification ranking [over.ics.rank]/3.2.5-6.
#include <cstdio>
struct S { const char *f() { return "non-const"; } const char *f() const { return "const"; } };
const char *g(int *) { return "int*"; }
const char *g(const int *) { return "const int*"; }
const char *h(int &) { return "int&"; }
const char *h(const int &) { return "const int&"; }
const char *k(int) { return "int"; }
const char *k(long) { return "long"; }
const char *m(char) { return "char"; }
const char *m(int) { return "int"; }
const char *p(const char *) { return "const char*"; }
const char *p(const void *) { return "const void*"; }
typedef int Int;
int main() {
    S s; const S cs; int i = 0; const int ci = 1; short sh = 2; Int ii = 3; char ch = 'c';
    std::printf("%s %s\n", s.f(), cs.f());
    std::printf("%s %s\n", g(&i), g(&ci));
    std::printf("%s %s %s\n", h(i), h(ci), h(4));
    std::printf("%s %s %s\n", k(sh), k(ii), k(5L));
    std::printf("%s %s %s\n", m(ch), m('x' + 1), m(sh));
    std::printf("%s %s\n", p("s"), p(&i));
    return 0;
}
