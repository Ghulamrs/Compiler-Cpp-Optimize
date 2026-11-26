// Literal types a constexpr function may take and return ([basic.types]/10): scalars, references to any type,
// pointers to non-literal classes, aggregates, and classes with a constexpr constructor or constructor template.
#include <cstdio>

struct Plain { int a, b; };
struct Point {
    int x, y;
    constexpr Point(int px, int py) : x(px), y(py) {}
};
struct Heavy {
    Heavy() : v(5) {}
    ~Heavy() {}
    int v;
};
struct Ctor {
    template <class T> constexpr Ctor(T t) : v(static_cast<int>(t)) {}
    int v;
};

constexpr int sum(Plain p) { return p.a + p.b; }
constexpr Point twice(Point p) { return Point(p.x * 2, p.y * 2); }
constexpr int byRef(const Heavy &h, int k) { return k; }
constexpr int byPtr(const Heavy *h) { return h == nullptr ? 1 : 2; }
constexpr int fromCtor(Ctor c) { return c.v; }

int main() {
    constexpr Plain p = { 3, 4 };
    static_assert(sum(p) == 7, "sum");
    static_assert(twice(Point(1, 2)).y == 4, "twice");
    Heavy h;
    std::printf("%d %d %d %d %d\n", sum(p), twice(Point(1, 2)).x, byRef(h, 9), byPtr(&h), fromCtor(Ctor(6.5)));
    return 0;
}
