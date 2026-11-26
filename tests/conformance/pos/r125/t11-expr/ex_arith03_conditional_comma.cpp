// [expr.cond]: lvalue result when both arms are lvalues of the same type, class type arms, throw in one arm, conversions of arms, nested; [expr.comma] value and type; sequencing within.
#include <cstdio>
struct S { int v; };
int f() { std::printf("f "); return 1; }
int g() { std::printf("g "); return 2; }
int main() {
    int a = 1, b = 2; bool t = true;
    (t ? a : b) = 10; (!t ? a : b) = 20; std::printf("%d %d\n", a, b);
    S s1 = {1}, s2 = {2}; (t ? s1 : s2).v = 5; std::printf("%d %d\n", s1.v, s2.v);
    double d = t ? 1 : 2.5; std::printf("%g %d\n", d, (int)sizeof(t ? 1 : 2.5));
    int x = t ? 7 : throw 1; std::printf("%d\n", x);
    const char *s = a > 5 ? "big" : "small"; std::printf("%s\n", s);
    int y = (f(), g(), 3); std::printf("%d\n", y);
    int z = (a, b); std::printf("%d\n", z);
    std::printf("%d\n", t ? a ? 1 : 2 : 3);
    long lv = t ? 1 : 2L; std::printf("%d\n", (int)sizeof(t ? 1 : 2L));
    unsigned cmp = t ? -1 : 1u; std::printf("%u\n", cmp);
    return 0;
}
