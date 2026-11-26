#include <cstdio>
#include <string>
struct M { int id; M() : id(1) { std::printf("M() "); } M(const M &o) : id(o.id + 10) { std::printf("M(M) "); }
           M &operator=(const M &o) { id = o.id + 100; std::printf("M= "); return *this; } ~M() { std::printf("~M "); } };
struct S {
    M m;
    int k = 7;
    std::string s;
    S();
    S(const S &);
    S &operator=(const S &);
    ~S();
};
S::S() = default;
S::S(const S &) = default;
S &S::operator=(const S &) = default;
S::~S() = default;
struct V { virtual ~V(); virtual int f() { return 3; } };
V::~V() = default;
int main() {
    {
        S a;
        a.s = "x";
        S b(a);
        S c;
        c = b;
        std::printf("\n%d %d %d %d %s\n", a.m.id, b.m.id, c.m.id, c.k, c.s.c_str());
        V *v = new V;
        std::printf("%d\n", v->f());
        delete v;
    }
    std::printf("\n");
    return 0;
}
