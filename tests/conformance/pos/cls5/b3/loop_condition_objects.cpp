#include <cstdio>
struct C {
    int v;
    C(int x) : v(x) { std::printf("+%d ", v); }
    C(const C &o) : v(o.v) { std::printf("c%d ", v); }
    ~C() { std::printf("-%d ", v); }
    explicit operator bool() const { return v > 0; }
};
int main() {
    int n = 3, k = 0;
    while (C c{n--}) {
        if (c.v == 2) continue;
        k += c.v;
    }
    std::printf("| %d\n", k);
    int i = 0;
    for (int j = 10; C d{3 - i}; i++, j++) {
        if (d.v == 1) break;
        std::printf("[%d %d] ", d.v, j);
    }
    std::printf("| %d\n", i);
    int m = 2;
    while (auto a = m--) std::printf("a%d ", a);
    std::printf("\n");
    return 0;
}
