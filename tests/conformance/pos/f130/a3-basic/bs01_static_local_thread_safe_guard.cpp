#include <cstdio>
struct S { S() { std::printf("ctor\n"); } int v = 4; };
int f() { static S s; return s.v; }
int main() { std::printf("%d %d\n", f(), f()); return 0; }
