#include <cstdio>
#include <utility>
struct E { int v; E(int v) : v(v) { std::printf("+ "); } E(E &&o) : v(o.v) { std::printf("mv "); } E(const E &o) : v(o.v) { std::printf("cp "); } ~E() { std::printf("- "); } };
int main() { try { E e(1); throw e; } catch (E &e) { std::printf("h%d ", e.v); } std::printf("| "); try { E e(2); throw std::move(e); } catch (E &e) { std::printf("h%d ", e.v); } std::printf("\n"); return 0; }
