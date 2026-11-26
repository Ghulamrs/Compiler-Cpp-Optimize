// [class.base.init]/6, /10 and [except.ctor]/2: delegation in classes with virtual bases - to a target defined later,
// through a chain of delegations, under a diamond, and with the delegating body throwing once the object is complete.
#include <cstdio>
struct V {
    int v;
    V() : v(1) { std::printf("V() "); }
    explicit V(int x) : v(x) { std::printf("V(%d) ", x); }
    ~V() { std::printf("~V "); }
};
struct Later : virtual V {
    int w;
    Later() : Later(4) { std::printf("Later() "); }
    explicit Later(int x) : V(x + 1), w(x) { std::printf("Later(int) "); }
};
struct Chain : virtual V {
    int w;
    Chain() : Chain('c') { std::printf("Chain() "); }
    explicit Chain(char c) : Chain(static_cast<int>(c), 1) { std::printf("Chain(char) "); }
    Chain(int a, int b) : V(a - b), w(a + b) { std::printf("Chain(int,int) "); }
};
struct A : virtual V {
    int a;
    A() : A(2) { std::printf("A() "); }
    explicit A(int x) : V(x * 10), a(x) { std::printf("A(int) "); }
};
struct B : virtual V {
    int b;
    B() : b(3) { std::printf("B() "); }
};
struct D : A, B {
    D() : D(7) { std::printf("D() "); }
    explicit D(int x) : V(x) { std::printf("D(int) "); }
};
struct Throws : virtual V {
    explicit Throws(int x) : V(x) { std::printf("Throws(int) "); }
    Throws() : Throws(6) { std::printf("Throws() "); throw 1; }
    ~Throws() { std::printf("~Throws "); }
};
struct Under : Throws {
    Under() : Throws() { std::printf("Under() "); }
};
int main() {
    {
        Later l;
        std::printf("| %d %d\n", l.v, l.w);
    }
    std::printf("\n");
    {
        Chain c;
        std::printf("| %d %d\n", c.v, c.w);
    }
    std::printf("\n");
    {
        A a;
        std::printf("| %d %d\n", a.v, a.a);
    }
    std::printf("\n");
    {
        D d;
        std::printf("| %d %d %d\n", d.v, d.a, d.b);
    }
    std::printf("\n");
    try {
        Throws t;
    } catch (int) {
        std::printf("caught\n");
    }
    try {
        Under u;
    } catch (int) {
        std::printf("caught\n");
    }
    return 0;
}
