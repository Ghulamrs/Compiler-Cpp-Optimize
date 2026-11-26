// [stmt.ambig]/1: a statement that opens `T(...)` declares only if what the parentheses hold can be a declarator;
// `T(4);` and `T(a + b);` are temporaries, `T(t);` declares a t.
#include <cstdio>
struct T {
    int v;
    explicit T(int a) : v(a) { std::printf("T%d ", a); }
    T(int a, int b) : v(a + b) { std::printf("T2 "); }
    T() : v(0) { std::printf("T0 "); }
    ~T() { std::printf("~T%d ", v); }
};
int a = 2, b = 3;
struct S { int x; };
int main() {
    S s = { 1 };
    T(4);
    T(1 + 2);
    T(a + b);
    T(a, b);
    T(-1);
    T(s.x);
    T(a * 2);
    std::printf("\n");
    T(t);
    t.v = 8;
    T (u)(5);
    T(*p) = &t;
    std::printf("%d %d\n", p->v, u.v);
    return 0;
}
