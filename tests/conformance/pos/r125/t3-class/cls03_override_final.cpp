// [class.virtual]: override, final on functions and classes, virtual destructor chains, pure virtual with body, calling virtual from ctor/dtor (resolves to the current class).
#include <cstdio>
struct B { virtual ~B() { std::printf("~B "); } virtual int f() const { return 1; } virtual int g() { return 10; } virtual void pure() = 0; B() { std::printf("B:%d ", g()); } };
void B::pure() { std::printf("B::pure "); }
struct D : B { ~D() override { std::printf("~D "); } int f() const override { return 2; } int g() final { return 20; } void pure() override { B::pure(); std::printf("D::pure "); } D() { std::printf("D:%d ", g()); } };
struct E final : D { int f() const override { return 3; } };
struct Abstract { virtual int v() = 0; int call() { return v() * 2; } };
struct Concrete : Abstract { int v() override { return 21; } };
int main() {
    { E e; B &b = e; std::printf("%d %d ", b.f(), b.g()); b.pure(); std::printf("\n"); }
    std::printf("\n");
    Concrete c; std::printf("%d\n", c.call());
    D d; B *p = &d; delete new E; std::printf("%d\n", p->f());
    std::printf("\n");
    return 0;
}
