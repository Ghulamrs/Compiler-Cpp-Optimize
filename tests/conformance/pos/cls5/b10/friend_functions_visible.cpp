// A function a friend declaration introduces is found through an argument's class ([basic.lookup.argdep]/4),
// and by ordinary lookup once declared or defined outside the class, before or after it.
#include <cstdio>

int before(int v);

class A {
    int x;
    friend int before(int v);
    friend int after(int v);
    friend int peek(const A &a) { return a.x; }
    friend int peekPtr(const A *a);
    friend int both(const A &a, int v);
public:
    explicit A(int v) : x(v) {}
};

int before(int v) { return v + 1; }
int after(int v);
int after(int v) { return v + 2; }
int peekPtr(const A *a) { return a->x * 10; }
int both(const A &a, int v) { return a.x + v; }

namespace n {
    struct B {
        int y;
        friend int get(const B &b) { return b.y; }
        friend int put(B &b, int v);
    };
    int put(B &b, int v) { b.y = v; return v; }
}

int main() {
    A a(4);
    n::B b = { 6 };
    put(b, 9);
    const int got = get(b);
    const int put11 = n::put(b, 11);
    std::printf("%d %d %d %d %d %d %d %d\n", before(1), after(1), peek(a), peekPtr(&a), both(a, 3), got, put11,
                get(b));
    return 0;
}
