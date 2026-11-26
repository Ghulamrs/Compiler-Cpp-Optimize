#include <cstdio>
struct S { ~S() noexcept(false) {} }; struct T { int v; }; struct U { S s; }; struct V { T t; }; struct W { W() noexcept(true) {} W(const W &) noexcept {} W &operator=(W &&) noexcept = default; };
void f() noexcept(sizeof(int) == 4); void g() throw(); void h() throw(int); template <class X> void t(X) noexcept(noexcept(X()));
int main() { std::printf("%d %d %d %d %d %d %d %d %d\n", (int)noexcept(f()), (int)noexcept(g()), (int)noexcept(h()), (int)noexcept(U()), (int)noexcept(V()), (int)noexcept(T()), (int)noexcept(t(1)), (int)noexcept(t(U())), (int)noexcept(W())); W w; W w2(w); (void)w2; return 0; }
void f() noexcept(sizeof(int) == 4) {} void g() throw() {} void h() throw(int) {} template <class X> void t(X) noexcept(noexcept(X())) {}
