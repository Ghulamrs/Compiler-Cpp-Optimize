#include <cstdio>
struct A { const char *n; A(const char *s) : n(s) { std::printf("+%s\n", n); } ~A() { std::printf("-%s\n", n); } };
A a("a"); A b("b");
A &make() { static A c("c"); return c; }
int main() { make(); static A d("d"); return 0; }
