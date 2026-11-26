#include <cstdio>
struct B { int v = 1; virtual int f() const { return v; } virtual ~B() {} }; struct D : B { int w = 5; int f() const override { return v + w; } };
int main() { D d; d.v = 2; B b = d; B b2; b2 = d; B &r = d; std::printf("%d %d %d %d\n", b.f(), b2.f(), r.f(), (int)sizeof(b)); return 0; }
