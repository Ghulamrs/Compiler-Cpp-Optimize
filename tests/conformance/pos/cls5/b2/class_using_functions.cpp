#include <cstdio>
struct A { int a = 1; int g(int) const { return a; } };
struct B { int b = 20; int f(int) { return b + 1; } int f(int, int) { return b + 2; } virtual int v() { return 30; } virtual ~B() {} };
struct D : A, B {
    using B::f;
    using A::g;
    int f(double) { return 3; }
    int f(int, int) { return 4; }
    int v() { return 31; }
    int g(long) const { return 5; }
    int inside() { return f(1) * 100 + f(2.0) * 10 + f(1, 1); }
};
class P { protected: int prot() const { return 6; } int x = 7; public: int pub() const { return 8; } };
class Q : private P { public: using P::prot; using P::pub; using P::x; };
template <class T> struct Base { int h(T) { return 9; } };
template <class T> struct Der : Base<T> { using Base<T>::h; int h(const char *) { return 10; } };
int main() {
    D d;
    B &br = d;
    Q q;
    Der<int> t;
    std::printf("%d %d %d %d %d %d\n", d.f(1), d.f(1.5), d.f(1, 2), d.inside(), d.g(1), d.g(1L));
    std::printf("%d %d %d %d %d %d\n", br.v(), q.prot(), q.pub(), q.x, t.h(1), t.h("s"));
    return 0;
}
