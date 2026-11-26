#include <cstdio>
struct S { int a = 4; int c = this->a * 2; S() {} };
int main() { S s; std::printf("%d %d\n", s.a, s.c); return 0; }
