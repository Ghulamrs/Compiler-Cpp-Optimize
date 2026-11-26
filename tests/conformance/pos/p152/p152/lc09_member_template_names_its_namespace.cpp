#include <cstdio>
struct Tag {};
template <class T, class U> struct P {
    int v;
    template <class A> P(Tag, A a) : v(a) {}
};
namespace N {
struct Tag2 {};
template <class T> struct Q {
    int v;
    template <class A> Q(Tag2 t, A a) : v(a) {}
};
template <class T> struct R {
    int v;
    R(Tag2, int a) : v(a) {}
};
}
int main() {
    P<int, int> a(Tag(), 3);
    N::Q<int> b(N::Tag2(), 4);
    N::R<int> c(N::Tag2(), 5);
    std::printf("%d %d %d\n", a.v, b.v, c.v);
}
