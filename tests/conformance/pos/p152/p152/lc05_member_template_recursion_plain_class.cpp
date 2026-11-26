#include <cstdio>
struct G { bool operator()(int a, int b) const { return a > b; } };
struct M {
    int n;
    explicit M(int k) : n(k) {}
    template <class C> void sort(C c) { sortBy(c); }
    template <class C> void sortBy(C comp) {
        if (n < 2) return;
        M left(n / 2);
        left.sortBy(comp);
        n = left.n + (comp(1, 2) ? 100 : 0);
    }
};
int main() {
    M m(5);
    m.sort(G());
    std::printf("%d\n", m.n);
}
