#include <cstdio>
struct H { int a[2] = {1, 2}; H() { std::printf("+"); } ~H() { std::printf("-"); } int *begin() { return a; } int *end() { return a + 2; } };
int main() { for (int x : H()) std::printf("%d", x); std::printf("|"); for (int x : H().a) std::printf("%d", x); std::printf("\n"); return 0; }
