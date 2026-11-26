// [over.match.oper]/2: `=` on a class is its operator=, a member function - callable on a prvalue, and on a
// const object when it is a const member.
#include <cstdio>
struct P {
    int *a;
    P(int &x) : a(&x) {}
    P &operator=(int v) { *a = v; return *this; }
};
P mk(int &x) { return P(x); }
template <class T> struct Q {
    T *a;
    Q(T &x) : a(&x) {}
    Q &operator=(const T &v) { *a = v; return *this; }
};
template <class T> Q<T> mq(T &x) { return Q<T>(x); }
struct Ignore {
    template <class U> const Ignore &operator=(const U &) const { return *this; }
};
const Ignore ignore = Ignore();
struct Pair {
    int *a, *b;
    Pair &operator=(const Pair &o) { *a = *o.a; *b = *o.b; return *this; }
};
Pair both(int &x, int &y) { Pair p = {&x, &y}; return p; }
int main() {
    int x = 1, y = 2, u = 7, w = 8;
    mk(x) = 5;
    mq(y) = 6;
    ignore = 3;
    ignore = 2.5;
    both(x, y) = both(u, w);
    std::printf("%d %d\n", x, y);
    return 0;
}
