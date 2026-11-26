#include <cstdio>
struct B { bool v; };
bool operator&&(B a, B b) { return a.v && b.v; }
bool operator||(B a, bool b) { return a.v || b; }
struct C { int n; int operator,(int i) const { return n * 10 + i; } };
enum E { e0, e1 };
int operator&&(E a, E b) { return 40 + a + b; }
struct S { int m; int k; int get() const { return m + k; } };
struct H {
    S *p;
    int &operator->*(int S::*pm) const { return p->*pm; }
};
struct I { int operator->*(int (S::*pf)() const) const { return (s.*pf)() * 2; } S s; };
int main() {
    B t = {true}, f = {false};
    C c = {3};
    S s = {4, 5};
    H h = {&s};
    I i = {{1, 2}};
    h->*&S::k = 6;
    int r = (c, 7);
    std::printf("%d %d %d %d %d %d %d %d\n", (int)(t && t), (int)(t && f), (int)(f || true), r, e1 && e1,
                h->*&S::m, s.k, i->*&S::get);
    return 0;
}
