// Declarations: multiple declarators with mixed pointer/array/reference; declaration in the middle of a block after statements; shadowing in nested scopes; a class defined inside a function and used; typedef in block; enum in block; extern declaration in block.
#include <cstdio>
int external = 77;
int main() {
    int a = 1, *p = &a, arr[2] = {2, 3}, &r = a, (*fp)(int) = 0;
    std::printf("%d %d %d %d %d\n", a, *p, arr[1], r, fp == 0);
    a = 5; { int a = 6; { int a = 7; std::printf("%d ", a); } std::printf("%d ", a); } std::printf("%d\n", a);
    struct Local { int v; int dbl() const { return v * 2; } } l = {4}; std::printf("%d\n", l.dbl());
    typedef Local L2; L2 m = {5}; std::printf("%d\n", m.v);
    enum { X = 3, Y } e = Y; std::printf("%d %d\n", X, e);
    extern int external; std::printf("%d\n", external);
    const int c = 9; int arr2[c]; arr2[8] = 1; std::printf("%d\n", arr2[8]);
    unsigned long long ull = 1ULL << 40; long double ld = 1.5L; std::printf("%llu %g\n", ull, (double)ld);
    int x1, x2; x1 = x2 = 8; std::printf("%d %d\n", x1, x2);
    return 0;
}
