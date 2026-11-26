#include <cstdio>
struct S { int a = 4; int c = this->a * 2; int d = a + 1; };
int main() { S s; std::printf("%d %d %d\n", s.a, s.c, s.d); return 0; }
