// [temp.over]/1: every member template of the name that deduces is a candidate, and the call ranks them -
// two that deduce It alike are told apart by a tag parameter; a static one is reached through a template-id.
#include <cstdio>
template <bool> struct Tag {};
struct T {
    int n;
    template <class It> void add(It, It, Tag<false>) { n = -1; }
    template <class It> void add(It a, It b, Tag<true>) { n = a * 100 + b; }
};
template <int I, int N> struct C {
    template <class X> static int less(const X &x) { return I + C<I + 1, N>::less(x); }
};
template <int N> struct C<N, N> {
    template <class X> static int less(const X &) { return 0; }
};
int main() {
    T t;
    t.add(3, 65, Tag<true>());
    int a = t.n;
    t.add(1, 2, Tag<false>());
    std::printf("%d %d %d %d\n", a, t.n, C<0, 3>::less(5), C<2, 3>::less('x'));
    return 0;
}
