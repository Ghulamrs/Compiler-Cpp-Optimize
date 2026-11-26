#include <cstdio>
template <class K> struct Base {
    int n;
    Base() : n(1) {}
    Base &operator=(const Base &o) { n = o.n + 10; return *this; }
    Base &operator<<=(int k) { n <<= k; return *this; }
};
template <class K> struct Der : public Base<K> {
    typedef Base<K> base;
    Der &operator=(const Der &o) { base::operator=(o); return *this; }
    Der shifted(int k) const { Der d(*this); return d; }
    Base<K> twice() const { return Base<K>(*this) <<= 1; }
};
int main() {
    Der<int> a, b;
    a = b;
    std::printf("%d %d\n", a.n, a.twice().n);
}
