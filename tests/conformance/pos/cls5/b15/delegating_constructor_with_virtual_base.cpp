// [class.base.init]/6 and /10: a delegating constructor's target builds the object, virtual bases included where
// the object is the most derived one; as a base subobject the most derived class builds them instead.
#include <cstdio>

struct V {
    int v;
    V() : v(1) { std::printf("V() "); }
    explicit V(int x) : v(x) { std::printf("V(%d) ", x); }
};
struct S : virtual V {
    int w;
    explicit S(int x) : V(x * 10), w(x) { std::printf("S(%d) ", x); }
    S() : S(5) { std::printf("S() "); }
    S(int a, int b) : w(a + b) { std::printf("S(%d,%d) ", a, b); }
    explicit S(double d) : S(static_cast<int>(d), 1) { std::printf("S(double) "); }
};
struct T : S {
    T() : V(9), S() { std::printf("T() "); }
    explicit T(int) : S(2.5) { std::printf("T(int) "); }
};

int main() {
    S a;
    std::printf("| %d %d\n", a.v, a.w);
    S b(3.5);
    std::printf("| %d %d\n", b.v, b.w);
    T c;
    std::printf("| %d %d\n", c.v, c.w);
    T d(0);
    std::printf("| %d %d\n", d.v, d.w);
    return 0;
}
