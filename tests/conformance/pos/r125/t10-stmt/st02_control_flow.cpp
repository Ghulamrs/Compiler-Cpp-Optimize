// if/while/for with declarations in conditions, switch with fallthrough/default in the middle/enum class/nested, goto forward and backward, labels, do-while, comma in for, multiple declarators in for-init, continue in do-while.
#include <cstdio>
int next(int &i) { return i < 3 ? ++i : 0; }
enum class K { A, B, C };
int main() {
    int i = 0; while (int v = next(i)) std::printf("%d ", v); std::printf("\n");
    if (int z = 5 - 5) std::printf("nz "); else std::printf("zero %d\n", z);
    for (int a = 0, b = 10; a < b; a += 3, b -= 3) std::printf("%d-%d ", a, b); std::printf("\n");
    int n = 0; do { if (++n % 2) continue; std::printf("%d ", n); } while (n < 6); std::printf("\n");
    for (int k = 0; k < 5; ++k) { switch (k) { default: std::printf("d"); case 1: std::printf("1"); break; case 2: case 3: std::printf("23"); break; case 4: { int local = k; std::printf("%d", local); } } std::printf(" "); } std::printf("\n");
    K kk = K::B; switch (kk) { case K::A: std::printf("A"); break; case K::B: std::printf("B"); case K::C: std::printf("C"); } std::printf("\n");
    int count = 0; again: ++count; if (count < 3) goto again; std::printf("%d\n", count);
    for (int x = 0; x < 10; ++x) { for (int y = 0; y < 10; ++y) { if (x * y == 12) { std::printf("%d %d\n", x, y); goto done; } } } done:
    switch (3) { case 3: std::printf("three\n"); }
    int w = 0; while (true) { if (++w > 4) break; } std::printf("%d\n", w);
    bool flag = false; if (!flag) {} else std::printf("bad\n"); std::printf("end\n");
    return 0;
}
