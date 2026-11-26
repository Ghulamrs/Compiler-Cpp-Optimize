#include <cstdio>
struct Base {
    int n;
    Base() : n(1) {}
    Base &operator=(const Base &o) { n = o.n + 10; return *this; }
    int get() const { return n; }
};
struct Der : public Base {
    typedef Base base;
    Der &operator=(const Der &o) { base::operator=(o); return *this; }
    int g() const { return base::get() + 1; }
};
template <class K> struct DT : public Base {
    typedef Base base;
    int g() const { return base::get() + 2; }
};
int main() {
    Der a, b;
    a = b;
    DT<int> c;
    std::printf("%d %d %d\n", a.n, a.g(), c.g());
}
