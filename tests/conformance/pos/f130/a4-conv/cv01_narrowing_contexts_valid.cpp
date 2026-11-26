#include <cstdio>
struct A { char c; short s; }; struct W { int i; W(int v) : i{v} {} };
int get() { return 3; }
int main() { const int big = 100; char c{big}; unsigned u{3}; float f{1.0}; double d{1.0f}; int i{'a'}; A a{1, 2}; A *p = new A{3, 4}; W w{5}; int arr[]{1, 2}; short sh{big + 1};
std::printf("%d %u %g %g %d %d %d %d %d %d %d\n", c, u, f, d, i, a.c, p->s, w.i, arr[1], sh, get()); delete p; return 0; }
