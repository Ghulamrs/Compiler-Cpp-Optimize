#include <cstdio>
void f() noexcept; void f() noexcept {} void (*pf)() noexcept = f; void (*pg)() = f; struct B { virtual void v() noexcept {} virtual void w() throw() {} virtual ~B() {} }; struct D : B { void v() noexcept override {} void w() throw() override {} };
int main() { D d; B &b = d; b.v(); b.w(); std::printf("%d %d\n", (int)noexcept(pf()), (int)noexcept(pg())); return 0; }
