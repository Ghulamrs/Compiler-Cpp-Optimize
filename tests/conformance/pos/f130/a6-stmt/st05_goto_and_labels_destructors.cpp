#include <cstdio>
struct T { int i; T(int i) : i(i) { std::printf("+%d", i); } ~T() { std::printf("-%d", i); } };
int main() { int n = 0; again: { T t(n); if (++n < 3) goto again; } { T u(9); goto out; } out: std::printf("\n"); return 0; }
