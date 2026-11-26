#include <cstdio>
struct A;
struct B {
    int peek(const A &a) const;
    int peek(const A &a, int k);
    int other(const A &a);
};
class A {
    int v = 7;
    friend int B::peek(const A &) const;
    friend int B::peek(const A &, int);
};
int B::peek(const A &a) const { return a.v; }
int B::peek(const A &a, int k) { return a.v * k; }
int B::other(const A &) { return 0; }
int main() {
    A a;
    B b;
    std::printf("%d %d %d\n", b.peek(a), b.peek(a, 3), b.other(a));
    return 0;
}
