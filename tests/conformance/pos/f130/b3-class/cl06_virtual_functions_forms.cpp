#include <cstdio>
struct B { virtual ~B() {} virtual int f() const { return 1; } virtual int g(int x = 10) const { return x; } virtual B *clone() const = 0; virtual int pure() const = 0; };
int B::pure() const { return 99; }
struct D : B { int f() const override { return 2; } int g(int x = 20) const override { return x + 1; } D *clone() const override { return new D(*this); } int pure() const override { return B::pure() + 1; } };
struct E final : D { int f() const final { return 3; } };
int main() { E e; B &b = e; D &d = e; B *c = b.clone(); std::printf("%d %d %d %d %d %d\n", b.f(), b.g(), d.g(), b.pure(), c->f(), e.g(5)); delete c; return 0; }
