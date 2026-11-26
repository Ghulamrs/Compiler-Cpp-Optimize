#include <cstdio>
struct A { int a = 1; }; struct B { int b = 2; }; struct C : A, B { int c = 3; };
int main() { C c; B *pb = &c; A *pa = &c; void *v1 = pa; void *v2 = pb; std::printf("%d %d %d %d\n", pb->b, pa->a, (int)(v1 == v2), (int)(static_cast<C*>(pb) == &c)); return 0; }
