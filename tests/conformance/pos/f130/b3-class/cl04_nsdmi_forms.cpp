#include <cstdio>
#include <string>
struct S { int a = 1; int b{2}; int c = a + b; std::string s = "str"; const char *p = s.c_str(); int *q = &a; static const int k = 5; int d = k * 2; S() {} S(int x) : a(x) {} };
int main() { S s; S t(10); std::printf("%d %d %d %s %d %d %d %d %d\n", s.a, s.b, s.c, s.s.c_str(), (int)(s.p == s.s.c_str()), (int)(s.q == &s.a), s.d, t.a, t.c); return 0; }
