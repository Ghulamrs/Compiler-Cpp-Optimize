#include <cstdio>
template <class T> struct L {
    int n;
    explicit L(int k) : n(k) {}
    void sort() { sortBy(Less()); }
    template <class C> void sort(C c) { sortBy(c); }
private:
    struct Less { bool operator()(const T &a, const T &b) const { return a < b; } };
    template <class C> void sortBy(C comp) {
        if (n < 2) return;
        L left(n / 2), right(n - n / 2);
        left.sortBy(comp);
        right.sortBy(comp);
        n = left.n + right.n + (comp(1, 2) ? 100 : 0);
    }
};
struct G { bool operator()(int a, int b) const { return a > b; } };
struct R {
    int n;
    template <class C> int rec(C c, int k) { return k == 0 ? 0 : 1 + R().rec(c, k - 1); }
};
int main() {
    L<int> a(5), b(4);
    a.sort();
    b.sort(G());
    std::printf("%d %d %d\n", a.n, b.n, R().rec(G(), 3));
}
