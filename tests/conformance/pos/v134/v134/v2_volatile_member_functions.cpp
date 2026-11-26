#include <cstdio>
struct S {
    int v;
    int get() { return 1; }
    int get() volatile { return 2; }
    int get() const { return 3; }
    int get() const volatile { return 4; }
    int only() const volatile { return v; }
};
int main() {
    S s = {5}; volatile S vs; vs.v = 6; const S cs = {7}; const volatile S cvs = {8};
    std::printf("%d %d %d %d\n", s.get(), vs.get(), cs.get(), cvs.get());
    std::printf("%d %d %d %d\n", s.only(), vs.only(), cs.only(), cvs.only());
    volatile S *p = &vs;
    int (S::*pm)() volatile = &S::get;
    std::printf("%d %d\n", p->get(), (vs.*pm)());
    return 0;
}
