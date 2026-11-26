// [over.over]/1: an overloaded function's name in a braced list is the one the element's type names.
#include <cstdio>
struct S { int f(int) { return 1; } int f(double) { return 2; } };
int h(int) { return 3; }
int h(double) { return 4; }
struct P { int (S::*pf)(int); int (S::*pd)(double); };
struct Q { int (*hi)(int); int (*hd)(double); };
int main() {
    P p = { &S::f, &S::f };
    Q q = { h, &h };
    int (*arr[2])(double) = { h, &h };
    S s;
    std::printf("%d %d %d %d %d\n", (s.*p.pf)(0), (s.*p.pd)(0.0), q.hi(0), q.hd(0.0), arr[1](1.0));
    return 0;
}