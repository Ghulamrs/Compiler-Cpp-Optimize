// R7: [temp.expl.spec]/16 - a member template's specialization for one specialization of its class, and
// the member template itself defined anew for another.
#include <cstdio>
template <class T> struct S {
    template <class U> int g(U) { return 1; }
    template <class U> int h(U u) { return (int)sizeof(u); }
};
template <> template <> int S<int>::g(char) { return 2; }
template <> template <> int S<int>::g<long>(long) { return 4; }
template <> template <class U> int S<char>::g(U) { return 3; }
int main() {
    S<int> a; S<char> c; S<long> l;
    std::printf("%d %d %d %d %d %d %d\n", a.g('x'), a.g(1), a.g(2L), c.g(1), c.g('y'), l.g('z'), a.h(1.0));
    return 0;
}