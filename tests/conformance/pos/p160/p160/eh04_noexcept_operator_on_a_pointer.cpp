// [expr.unary.noexcept]/3: a call through a pointer declared `noexcept` cannot throw, one through any other can -
// the specification is the declared pointer's, a namespace-scope one or a local.
#include <cstdio>
void f() noexcept {}
void g() {}
void (*pf)() noexcept = f;
void (*pg)() = f;
int main() {
    void (*lf)() noexcept = f;
    void (*lg)() = g;
    const int a = noexcept(pf());
    const int b = noexcept(pg());
    const int c = noexcept(lf());
    const int d = noexcept(lg());
    const int e = noexcept((*lf)());
    std::printf("%d %d %d %d %d\n", a, b, c, d, e);
    {
        void (*pf)() = g;
        const int shadowed = noexcept(pf());
        std::printf("%d\n", shadowed);
    }
    return 0;
}
