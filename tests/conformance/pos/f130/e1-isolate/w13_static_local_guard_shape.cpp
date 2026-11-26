#include <cstdio>
struct S { S() {} int v = 4; };
int f() { static S s; return s.v; }
int main() { std::printf("%d\n", f()); return 0; }
