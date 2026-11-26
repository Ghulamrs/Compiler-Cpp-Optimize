#define HEADER <cstdio>
#include HEADER
#define ZERO 0
#define ONE (ZERO + 1)
#if ONE == 1 && defined ONE && !defined(TWO)
int a = 1;
#elif 1 / ZERO
int a = 2;
#endif
int main() { std::printf("%d\n", a); return 0; }
