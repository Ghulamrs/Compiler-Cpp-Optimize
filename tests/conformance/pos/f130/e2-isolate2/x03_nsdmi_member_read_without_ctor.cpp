#include <cstdio>
struct S { int a = 4; int d = a + 1; };
int main() { S s; std::printf("%d %d\n", s.a, s.d); return 0; }
