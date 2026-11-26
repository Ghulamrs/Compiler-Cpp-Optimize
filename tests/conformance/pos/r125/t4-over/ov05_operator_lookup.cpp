// [over.match.oper]: member vs non-member vs built-in candidates; ADL for operators in namespaces; operator on enum; friend operator defined in class found only by ADL; comma and -> overloads.
#include <cstdio>
namespace N {
    struct S { int v; S(int v) : v(v) {} S operator+(int k) const { return S(v + k); } };
    S operator+(int k, const S &s) { return S(k * 100 + s.v); }
    S operator-(const S &a, const S &b) { return S(a.v - b.v); }
    enum Color { Red = 1, Blue = 2 };
    Color operator|(Color a, Color b) { return Color((int)a | (int)b); }
    struct Hidden { int v; friend Hidden operator*(Hidden a, Hidden b) { return Hidden{a.v * b.v}; } };
}
struct Arith { int v; operator int() const { return v; } };
struct Mem { int v; bool operator!() const { return v == 0; } Mem operator~() const { return Mem{-v}; } };
int main() {
    N::S a(1), b(2);
    std::printf("%d %d %d\n", (a + 5).v, (5 + a).v, (a - b).v);
    N::Color c = N::Red | N::Blue; std::printf("%d\n", (int)c);
    N::Hidden h1 = {3}, h2 = {4}; std::printf("%d\n", (h1 * h2).v);
    Arith x = {6}, y = {7}; std::printf("%d %d\n", x + y, x < y);
    Mem m = {0}; std::printf("%d %d\n", !m, (~m).v);
    return 0;
}
