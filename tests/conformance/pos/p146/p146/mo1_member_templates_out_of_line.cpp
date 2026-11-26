// [temp.mem]/1: member function templates declared in their class and defined outside it - of a class,
// static and const among them, and of a class template with the class's list before the member's - and
// [temp.decls]: one only declared, the sizeof idiom's overloads, used where nothing is evaluated.
#include <cstdio>
struct S {
    template <class T> T conv(T) const;
    template <class T> static T st(T);
    template <class A, class B> int both(A, B) const;
};
template <class T> T S::conv(T t) const { return t + 1; }
template <class U> U S::st(U u) { return u * 2; }
template <class X, class Y> int S::both(X x, Y y) const { return (int)sizeof(X) * 10 + (int)sizeof(Y) + (int)x + (int)y; }
template <class T> struct W {
    T base;
    template <class U> U add(U) const;
    template <class U> static int size(U);
};
template <class T> template <class U> U W<T>::add(U u) const { return u + (U)base; }
template <class Q> template <class R> int W<Q>::size(R) { return (int)(sizeof(Q) * 100 + sizeof(R)); }
typedef char yes[1];
typedef char no[2];
template <class T> struct has_size {
    template <class U> static yes &test(char (*)[sizeof(&U::size)]);
    template <class U> static no &test(...);
    static const bool value = sizeof(test<T>(0)) == sizeof(yes);
};
struct Sized { int size() const { return 1; } };
int main() {
    S s;
    W<int> w = {5};
    W<char> c = {'\1'};
    std::printf("%d %g %d %d\n", s.conv(1), S::st(1.25), s.both('a', 2.5), s.both(1, 2));
    std::printf("%d %g %d %d %d\n", w.add(3), w.add(0.5), c.add(1), W<int>::size('x'), W<char>::size(1.0));
    std::printf("%d %d\n", (int)has_size<Sized>::value, (int)has_size<int>::value);
    return 0;
}
