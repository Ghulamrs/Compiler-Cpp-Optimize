#include <cstdio>
struct S { int a = 4; int d = sizeof(*this); S() {} };
int main() { S s; std::printf("%d %d\n", s.a, s.d); return 0; }
