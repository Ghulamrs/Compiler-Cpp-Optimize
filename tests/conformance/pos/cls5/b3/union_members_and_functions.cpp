#include <cstdio>
#include <new>
union Num { int i; float f; int get() const { return i; } void set(int v) { i = v; } static int twice(int v) { return v * 2; } };
struct S { int v; S() : v(1) { std::printf("S() "); } S(int x) : v(x) { std::printf("S(%d) ", x); } ~S() { std::printf("~S(%d) ", v); } };
struct Str { char c[4]; Str(const char *s) { for (int k = 0; k < 4; k++) c[k] = s[k]; } Str(const Str &o) { *this = o; } ~Str() { std::printf("~Str "); } };
union U {
    S s;
    int i;
    U() : s() {}
    U(int x) : i(x) {}
    ~U() {}
};
union W { Str str; long n; W() : n(5) {} ~W() {} };
int main() {
    Num n;
    n.set(21);
    {
        U a;
        U b(7);
        std::printf("| %d %d | ", a.s.v, b.i);
        a.s.~S();
        new (&b.s) S(9);
        std::printf("| %d | ", b.s.v);
        b.s.~S();
    }
    W w;
    std::printf("%ld ", w.n);
    new (&w.str) Str("abc");
    std::printf("%d %d %s ", n.get(), Num::twice(4), w.str.c);
    w.str.~Str();
    std::printf("\n");
    return 0;
}
