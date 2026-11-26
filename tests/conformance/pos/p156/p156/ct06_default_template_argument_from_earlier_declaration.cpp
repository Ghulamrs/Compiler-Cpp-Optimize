#include <cstdio>

// [temp.param]/10: the default written on the first declaration, under its own parameter names, is the
// template's - the definition names the parameters otherwise.
template <class T> struct Traits { typedef T type; static const int size = static_cast<int>(sizeof(T)); };
template <class C, class R = Traits<C> > struct Iter;
template <class C, class R = Traits<C>, class S = Traits<R> > struct Pair;

template <class X, class Y> struct Iter {
    typename Y::type v;
    int size() const { return Y::size; }
};
template <class A, class B, class D> struct Pair {
    int sizes() const { return B::size * 10 + static_cast<int>(sizeof(typename D::type)); }
};

int main() {
    Iter<short> i;
    i.v = 3;
    Pair<char> p;
    std::printf("%d %d %d\n", i.v, i.size(), p.sizes());
    return 0;
}
