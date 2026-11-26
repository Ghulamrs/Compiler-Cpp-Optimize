#include <cstdio>
int f(int, ...) { return 1; } int f(int, int) { return 2; } int f(int, double) { return 3; } int g(int a, int b = 1) { return a + b; } int g(int, int, int) { return 100; } int h(...) { return 1; } int h(const char *) { return 2; }
int main() { std::printf("%d %d %d %d %d %d %d\n", f(1, 2), f(1, 2.0), f(1, "s"), f(1), g(1), g(1, 2, 3), h(nullptr)); std::printf("%d %d\n", h("s"), h(1)); return 0; }
