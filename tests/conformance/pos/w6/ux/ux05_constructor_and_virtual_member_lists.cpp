// [except.spec]/9 over a constructor's mem-initialisers and a virtual member's body: the members built are destroyed, then std::unexpected()
#include <cstdio>
#include <exception>
struct M { int v; M(int x) : v(x) { std::printf("M%d ", v); } ~M() { std::printf("~M%d ", v); } };
int boom(int x) { if (x == 2) throw 2.5; return x; }
struct S {
    M a, b;
    S(int x) throw(int) : a(1), b(boom(x)) { std::printf("S "); }
    virtual int get() throw(int) { throw 'q'; }
};
struct D : S { D() : S(1) {} int get() throw(int) { return 4; } };
void onUnexpected() { std::printf("unexpected "); throw 9; }
int main() {
    std::set_unexpected(onUnexpected);
    try { S s(2); } catch (int v) { std::printf("| int %d\n", v); }
    try { S s(3); s.get(); } catch (int v) { std::printf("| int %d\n", v); }
    D d;
    S &r = d;
    std::printf("| %d\n", r.get());
    return 0;
}
