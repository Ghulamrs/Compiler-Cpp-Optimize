#include <cstdio>
int main() { int x = 1; int *p = &x; const int *const *pp = &p; int *const *pq = &p; const int *const &r = p; std::printf("%d %d %d\n", **pp, **pq, *r); return 0; }
