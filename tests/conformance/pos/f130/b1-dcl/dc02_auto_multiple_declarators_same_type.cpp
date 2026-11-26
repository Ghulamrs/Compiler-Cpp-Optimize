#include <cstdio>
int main() { auto a = 1, *b = &a, c = 3; auto x = 1.5, y = 2.5; std::printf("%d %d %d %g\n", a, *b, c, x + y); return 0; }
