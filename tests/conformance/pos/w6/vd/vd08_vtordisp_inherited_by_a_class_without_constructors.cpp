// [class.virtual]: a vtordisp a base put in front of the virtual base, kept by a class that declares no constructor
#include <cstdio>
struct V { virtual int f() { return 1; } };
struct B : virtual V { int b; B() : b(2) {} int f() { return b; } };
struct E : B { int e = 5; };
struct F : E { int f() { return e * 10; } };
int main() { E e; F g; V *p = &e, *q = &g; std::printf("%d %d %d\n", p->f(), q->f(), (int)(sizeof(E) == sizeof(B) + 8)); return 0; }
