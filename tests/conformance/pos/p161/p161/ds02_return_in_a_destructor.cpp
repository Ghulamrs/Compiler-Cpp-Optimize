// [class.dtor]/8: a `return` in a destructor goes on to the members and the bases, from a loop, a try block and a
// handler as much as from the body; a lambda's own `return` in it is the lambda's.
#include <cstdio>
struct Q { int id; explicit Q(int i) : id(i) {} ~Q() { std::printf("~Q%d ", id); } };
struct B { ~B() { std::printf("~B "); } };
struct K : B {
    Q q;
    int x;
    explicit K(int a) : q(1), x(a) {}
    ~K() {
        Q local(2);
        auto f = [](int v) { if (v) return; std::printf("lam "); };
        f(1);
        f(0);
        if (x == 1) return;
        for (int i = 0; i < 3; i++) { Q in(3); if (x == 2 && i == 1) return; }
        try { if (x == 3) return; throw 1; } catch (int) { if (x == 4) return; std::printf("handler "); }
        std::printf("tail ");
    }
};
struct T : B { Q a; T() : a(5) {} ~T() noexcept(false) { std::printf("~T "); return; } };
int main() {
    for (int x = 1; x <= 5; x++) {
        { K k(x); }
        std::printf("\n");
    }
    { T t; }
    std::printf("\n");
    return 0;
}
