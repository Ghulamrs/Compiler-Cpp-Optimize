// [dcl.init.ref]/5: a reference to a base binds a derived object directly, with no constructor asked - a converting
// template included, whose own parameter would otherwise be one more conversion.
#include <cstdio>
template <class T> struct A { int id; explicit A(int i) : id(i) {} template <class U> A(const A<U> &o) : id(o.id + 1) {} };
template <class T> struct D : A<T> { explicit D(int i) : A<T>(i) {} };
static int byRef(const A<int> &a) { return a.id; }
static int other(const A<char> &a) { return a.id; }
int main() {
    D<int> d(5);
    std::printf("%d %d\n", byRef(d), other(d));
    return 0;
}
