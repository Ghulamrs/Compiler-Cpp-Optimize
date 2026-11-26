// [over.match.copy]/1, [over.match.ctor]/1: copy-initialisation's candidates are the converting constructors.
// `A a = 1` takes A(double) past the explicit A(int); `W b = w` the copy constructor past an explicit template
// whose body would not compile.
#include <cstdio>
struct A {
    int k;
    explicit A(int) : k(1) {}
    A(double) : k(2) {}
};
struct W {
    int v;
    template <class U> explicit W(U &&u) : v(u.nope) {}
    W(const W &o) : v(o.v + 100) {}
    W(int x) : v(x) {}
};
int main() {
    A a = 1;
    A b(1);
    W w(5);
    W c = w;
    std::printf("%d %d %d\n", a.k, b.k, c.v);
    return 0;
}
