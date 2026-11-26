// [class.copy]/31: `throw E(1)` builds the exception object where it stays, no copy and no move - and a copy still
// goes where the operand is a named object.
#include <cstdio>
struct E {
    int v;
    explicit E(int a) : v(a) { std::printf("+%d ", v); }
    E(const E &o) : v(o.v + 10) { std::printf("c%d ", v); }
    ~E() { std::printf("-%d ", v); }
};
struct Moves {
    int v;
    explicit Moves(int a) : v(a) { std::printf("+%d ", v); }
    Moves(const Moves &o) : v(o.v + 10) { std::printf("c%d ", v); }
    Moves(Moves &&o) noexcept : v(o.v + 20) { std::printf("m%d ", v); }
    ~Moves() { std::printf("-%d ", v); }
};
int main() {
    try { throw E(1); } catch (const E &e) { std::printf("h%d ", e.v); }
    std::printf("\n");
    try { throw E(2); } catch (E e) { std::printf("h%d ", e.v); }
    std::printf("\n");
    try { E named(3); throw named; } catch (const E &e) { std::printf("h%d ", e.v); }
    std::printf("\n");
    try { throw Moves(4); } catch (const Moves &e) { std::printf("h%d ", e.v); }
    std::printf("\n");
    try { Moves named(5); throw named; } catch (const Moves &e) { std::printf("h%d ", e.v); }
    std::printf("\n");
    return 0;
}
