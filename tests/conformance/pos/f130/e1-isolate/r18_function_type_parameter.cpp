#include <cstdio>
int apply(int c(int), int x) { return c(x); } int twice(int x) { return x * 2; }
int main() { std::printf("%d\n", apply(twice, 2)); return 0; }
