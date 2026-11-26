#include <cstdio>
struct S { int a = 4; int b = sizeof(a) + sizeof(S::a); int c = this->a * 2; int d = sizeof(*this); };
int main() { S s; std::printf("%d %d %d %d\n", s.a, s.b, s.c, s.d); return 0; }
