#include <cstdio>
union U { int a = 5; float f; }; union V { int a; char c; V() : c('q') {} };
int main() { U u; V v; std::printf("%d %c %d\n", u.a, v.c, (int)sizeof(U)); return 0; }
