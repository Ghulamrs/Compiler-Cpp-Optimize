#include <cstdio>
struct E {}; struct E2 {}; struct D : E { int x; }; struct DD : E, E2 { int x; }; struct Has { E e; int x; }; struct Fn { void f() {} }; struct Derived2 : E { E e; int x; };
int main() { std::printf("%d %d %d %d %d %d %d\n", (int)sizeof(E), (int)sizeof(D), (int)sizeof(DD), (int)sizeof(Has), (int)sizeof(Fn), (int)sizeof(E[3]), (int)sizeof(Derived2)); return 0; }
