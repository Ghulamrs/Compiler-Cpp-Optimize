#include <cstdio>
#include <initializer_list>
int f(std::initializer_list<int>) { return 1; } int f(int) { return 2; } int h(long) { return 3; } int h(std::initializer_list<long>) { return 4; }
int main() { std::printf("%d %d %d %d\n", f({1}), f({1, 2}), f(1), h({1})); return 0; }
