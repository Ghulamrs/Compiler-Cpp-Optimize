#include <cstdio>
struct E { int v; E(int v) : v(v) { std::printf("+%d ", v); } E(const E &o) : v(o.v + 10) { std::printf("c%d ", v); } ~E() { std::printf("-%d ", v); } };
int main() { try { throw E(1); } catch (E e) { std::printf("h%d ", e.v); } std::printf("\n"); return 0; }
