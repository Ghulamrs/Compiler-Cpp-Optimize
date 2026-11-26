#include <utility>
#include <cstdio>
template <class K, bool U> struct Base {
    int n;
    explicit Base(int k) : n(k) {}
    Base(const Base &o, int a) : n(o.n + 10 + a) {}
    Base(Base &&o, int a) : n(o.n + 100 + a) { o.n = 0; }
    Base(const Base &o) : n(o.n + 10) {}
    Base(Base &&o) : n(o.n + 100) { o.n = 0; }
};
template <class K> struct Der : public Base<K, true> {
    typedef Base<K, true> base;
    explicit Der(int k) : base(k) {}
    Der(const Der &o) : base(o) {}
    Der(Der &&o) : base(std::move(o)) {}
    Der(Der &&o, int a) : base(std::move(o), a) {}
};
int main() {
    Der<int> a(1);
    Der<int> b(std::move(a));
    Der<int> c(std::move(b), 5);
    Der<int> d(c);
    std::printf("%d %d %d %d\n", a.n, b.n, c.n, d.n);
}
