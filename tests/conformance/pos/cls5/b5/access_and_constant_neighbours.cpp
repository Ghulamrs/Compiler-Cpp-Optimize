#include <cstdio>
class B { protected: int p = 1; static int sp; public: virtual ~B() {} };
int B::sp = 7;
class D : public B {
public:
    int own(D &d) { return d.p + p; }
    int stat(B &) { return sp; }
};
class Priv { ~Priv() {} friend void make(); public: static Priv *create() { return new Priv; } static void destroy(Priv *p) { delete p; } int v = 3; };
void make() { Priv p; std::printf("%d ", p.v); }
struct C { int a, b; constexpr C(int x) : a(x), b(x + 1) {} };
struct N { int n = 2; constexpr N() {} };
struct M { char c{100}; long long w{-5}; unsigned u{7}; };
enum Flags { High = 1 << 30, Top = 1 << 31 };
struct Copy { Copy() {} Copy(const Copy &) {} };
Copy makeCopy() { return Copy(); }
int main() {
    D d;
    D e;
    make();
    Priv *q = Priv::create();
    int v = q->v;
    Priv::destroy(q);
    constexpr C c(4);
    constexpr N n;
    M m;
    Copy cp = makeCopy();
    (void)cp;
    constexpr long long big = 2147483647LL + 1;
    constexpr unsigned wrap = 4294967295u + 1u;
    std::printf("%d %d %d %d %d %d %d %d %lld %u %d\n", d.own(e), d.stat(e), v, c.b, n.n, 9, (int)m.c, (int)m.w, big,
                wrap, (int)(Top < 0));
    return 0;
}
