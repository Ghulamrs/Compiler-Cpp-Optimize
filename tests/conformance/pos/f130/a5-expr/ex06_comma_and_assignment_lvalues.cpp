#include <cstdio>
int main() { int a, b, c; a = b = c = 3; (a, b) = 5; int &r = (a += 1, b); r = 10; int x = (1, 2, 3); std::printf("%d %d %d %d\n", a, b, c, x); return 0; }
