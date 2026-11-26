// [class.conv.ctor] and [class.copy]/2: an explicit constructor template, and a converting one that is never the copy constructor.
#include <cstdio>
template <class T> struct P {
    T *p;
    template <class Y> explicit P(Y *y) : p(y) { std::printf("Y* %d\n", (int)sizeof(Y)); }
    P(const P &o) : p(o.p) { std::printf("copy\n"); }
    template <class Y> P(const P<Y> &o) : p(o.p) { std::printf("convert\n"); }
};
struct Base { int b; }; struct Derived : Base { int d[3]; };
int main() {
    P<Base> a(new Derived);
    P<Base> b(a);
    P<Derived> c(new Derived);
    P<Base> d(c);
    std::printf("%d\n", (int)(b.p == a.p));
    delete static_cast<Derived *>(a.p); delete c.p;
    return 0;
}
