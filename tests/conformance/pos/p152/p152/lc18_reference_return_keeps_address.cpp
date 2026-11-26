#include <string>
#include <cstdio>
#include <stdexcept>
struct H {
    int k;
    H &put(const std::string &s) { k += (int)s.size(); return *this; }
};
H g = { 0 };
H &viaMember(H &h, int n) { return h.put(std::string(n, 'x')); }
H &inTry(H &h, int n) {
    try {
        return h.put(std::string(n, 'x'));
    } catch (const std::exception &) {
        return h;
    }
}
H &inCatch(H &h, int n) {
    try {
        throw std::runtime_error("x");
    } catch (const std::exception &e) {
        return h.put(std::string(n, 'q') + e.what());
    }
}
int main() {
    H &a = viaMember(g, 3);
    a.k += 100;
    H &b = inTry(g, 2);
    b.k += 1000;
    H &c = inCatch(g, 1);
    std::printf("%d %d %d %d\n", g.k, (int)(&a == &g), (int)(&b == &g), (int)(&c == &g));
}
