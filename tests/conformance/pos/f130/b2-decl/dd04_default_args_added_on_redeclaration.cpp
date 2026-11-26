#include <cstdio>
int f(int a, int b, int c); int f(int a, int b, int c = 3); int f(int a, int b = 2, int c); int f(int a, int b, int c) { return a * 100 + b * 10 + c; }
int main() { std::printf("%d %d %d\n", f(1), f(1, 5), f(1, 5, 7)); return 0; }
