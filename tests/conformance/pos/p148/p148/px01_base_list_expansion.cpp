// [temp.variadic]/4: a pack expanded in a base-specifier-list and in a mem-initializer-list, with and without arguments.
#include <cstdio>
struct A { int v; A() : v(1) {} A(int x) : v(x) {} int a() const { return v; } };
struct B { int v; B() : v(2) {} B(int x) : v(x * 10) {} int b() const { return v; } };
struct C { int v; C() : v(3) {} C(int x) : v(x * 100) {} int c() const { return v; } };
template <class... Bs> struct Plain : Bs... {};
template <class... Bs> struct Made : Bs... { Made() : Bs()... {} explicit Made(int x) : Bs(x)... {} };
template <class... Bs> struct Mixed : public Bs... { int own; Mixed(int x) : Bs(x + 1)..., own(x) {} };
template <class... Bs> int count(const Plain<Bs...> &) { return (int)sizeof...(Bs); }
int main() {
    Plain<A, B> p;
    Made<A, B, C> m, n(4);
    Mixed<A, C> x(5);
    Plain<> e;
    std::printf("%d %d %d\n", p.a(), p.b(), count(p));
    std::printf("%d %d %d | %d %d %d\n", m.a(), m.b(), m.c(), n.a(), n.b(), n.c());
    std::printf("%d %d %d %d\n", x.a(), x.c(), x.own, count(e));
    return 0;
}
