#include <cstdio>
typedef int I; typedef int I; struct S; typedef struct S S_t; struct S { int v; }; typedef struct { int w; } Anon; enum E { e0 }; typedef enum E E_t; class S_t2; typedef class S_t2 Cls;
int main() { S_t s{1}; Anon a{2}; E_t e = e0; struct S *p = &s; union U; std::printf("%d %d %d %d\n", s.v, a.w, e, p->v); return 0; }
