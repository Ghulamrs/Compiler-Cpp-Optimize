#include <cstdio>
struct S { int (*f)(); S() : f([] { return 3; }) {} int g() { return f(); } };
int main() { S s; std::printf("%d\n", s.g()); return 0; }
