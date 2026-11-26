#include <cstdio>
struct B { virtual int f() { return 1; } virtual int g() { return 1; } virtual ~B() {} }; struct D : B { int f() override final { return 2; } virtual int g() override { return 2; } }; struct E final : D { int g() override { return 3; } };
int main() { E e; B &b = e; std::printf("%d %d\n", b.f(), b.g()); return 0; }
