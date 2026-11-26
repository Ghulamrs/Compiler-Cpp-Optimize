// [temp.friend]: a friend defined in a class template of a namespace is that namespace's, found by its argument, and
// defined wherever the specialization was asked for.
#include <cstdio>
namespace N {
template <class T> struct Box {
    T v;
    friend bool operator==(const Box &a, const Box &b) { return a.v == b.v; }
    friend T get(const Box &b) { return b.v; }
    friend void swap2(Box &a, Box &b) { T t = a.v; a.v = b.v; b.v = t; }
};
namespace In { template <class T> struct Deep { T v; friend T twice(const Deep &d) { return d.v + d.v; } }; }
}
template <class T> int use(const N::Box<T> &b) { return (int)get(b); }
int main() {
    N::Box<int> x = { 1 }, y = { 2 };
    N::Box<char> c = { 'a' };
    N::In::Deep<double> d = { 1.5 };
    std::printf("%d %d %d\n", (int)(x == y), get(x), use(c));
    swap2(x, y);
    std::printf("%d %g\n", x.v, twice(d));
    return 0;
}
