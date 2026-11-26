#include <cstdio>
#include <utility>
struct M { M() {} M(const M &) { std::printf("cc "); } M(M &&) { std::printf("mc "); } M &operator=(const M &) { std::printf("ca "); return *this; } M &operator=(M &&) { std::printf("ma "); return *this; } };
struct S { M m; int i = 0; };
int main() { S a, b; b = std::move(a); S c(std::move(b)); b = a; std::printf("\n"); (void)c; return 0; }
