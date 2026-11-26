#include <cstdio>
#include <typeinfo>
struct S { int m; int k; static int s; enum { e = 3 }; void f(int a = s, int b = e, int c = sizeof(S)) { std::printf("%d %d %d ", a, b, c); } };
int S::s = 2;
int g(int) { return 1; }
struct A { virtual int f() = 0; virtual ~A() {} };
struct B : A { int f() { return 4; } };
struct alignas(16) W { int x; };
struct E { E() {} E(E &&) {} private: E(const E &) {} };
struct P { int v; P(int x) : v(x) {} P operator++(int) { P o = *this; v++; return o; } operator int() const { return v; } };
struct Q { virtual void f() throw(int, double) {} virtual void g() {} virtual void h() noexcept {} virtual ~Q() {} };
struct R : Q { void f() throw(int) {} void g() throw(char) {} void h() noexcept {} };
extern int ext;
extern int ext;
int ext = 5;
static int st = 6;
extern int st;
int main() {
    S s = {1, 2};
    s.f();
    std::printf("%d %d ", (int)sizeof(&g), (int)sizeof(g(1)));
    try { throw E(); } catch (E &) { std::printf("E "); }
    try { throw 1; } catch (void *) {} catch (int) { std::printf("int "); }
    B b;
    A &a = b;
    std::printf("%d %d %d ", a.f(), (int)alignof(W), (int)(typeid(a) == typeid(B)));
    P p(7);
    P q = p++;
    auto l = [](bool x) { if (x) return 1; return 2; };
    int S::*pm = &S::k;
    std::printf("%d %d %d %d %d %d\n", (int)p, (int)q, l(false), s.*pm, ext, st);
    return 0;
}
