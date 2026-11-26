// [dcl.constexpr]/3-4: literal types with constexpr constructors; constexpr objects; member access in constant expressions; constexpr static members of class type; constexpr functions returning class objects.
#include <cstdio>
struct Pt { int x, y; constexpr Pt(int x, int y) : x(x), y(y) {} constexpr int sum() const { return x + y; } constexpr Pt flip() const { return Pt(y, x); } };
constexpr Pt origin(0, 0);
constexpr Pt p(3, 4);
constexpr Pt q = p.flip();
constexpr int sumOf(const Pt &a) { return a.sum(); }
struct Holder { static constexpr Pt pt = Pt(5, 6); static constexpr int k = 7; };
constexpr int Holder::k;
constexpr Pt Holder::pt;
template <int N> struct W { static const int v = N; };
struct Lit { int a; constexpr Lit() : a(1) {} };
constexpr Lit l;
int main() {
    static_assert(p.sum() == 7, "sum"); static_assert(q.x == 4, "flip"); static_assert(sumOf(p) == 7, "ref"); static_assert(Holder::pt.y == 6, "static");
    std::printf("%d %d %d %d\n", W<p.sum()>::v, W<q.y>::v, W<Holder::k>::v, W<l.a>::v);
    std::printf("%d %d %d\n", origin.x, p.flip().x, Holder::pt.x);
    Pt r(7, 8); std::printf("%d %d\n", r.sum(), r.flip().y);
    constexpr int arrN = Pt(1, 2).sum(); int arr[arrN] = {0}; std::printf("%d\n", (int)sizeof(arr) / 4);
    return 0;
}
