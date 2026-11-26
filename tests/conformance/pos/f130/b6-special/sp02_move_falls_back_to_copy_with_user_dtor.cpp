#include <cstdio>
#include <utility>
struct S { S() {} S(const S &) { std::printf("copy "); } ~S() {} }; struct T { T() {} T(const T &) { std::printf("tcopy "); } T(T &&) { std::printf("tmove "); } };
struct Both { S s; T t; };
int main() { S a; S b(std::move(a)); T c; T d(std::move(c)); Both e; Both f(std::move(e)); Both g = e; std::printf("\n"); (void)b; (void)d; (void)f; (void)g; return 0; }
