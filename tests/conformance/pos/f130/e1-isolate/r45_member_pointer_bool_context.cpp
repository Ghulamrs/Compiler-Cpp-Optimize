#include <cstdio>
struct S { int a; };
int main() { int S::*p = &S::a; int S::*n = nullptr; if (p) std::printf("p "); if (!n) std::printf("n"); std::printf("\n"); return 0; }
