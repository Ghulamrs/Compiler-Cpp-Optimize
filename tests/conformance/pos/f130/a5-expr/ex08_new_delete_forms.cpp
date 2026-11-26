#include <cstdio>
#include <new>
struct T { int v; T() : v(5) {} T(int x) : v(x) {} ~T() { std::printf("~%d ", v); } };
int main() { T *a = new T; T *b = new T(7); T *c = new T{8}; T *d = new T[2]; T *e = new T[2]{1, 2}; int *f = new int[3]{1, 2, 3}; int *g = new int(); int *h = new int; auto *i = new auto(9); char buf[sizeof(T)]; T *j = new (buf) T(11); int *k = new (std::nothrow) int(12);
std::printf("%d %d %d %d %d %d %d %d %d %d %d\n", a->v, b->v, c->v, d[1].v, e[1].v, f[2], *g, *i, j->v, *k, (int)(h != nullptr)); delete a; delete b; delete c; delete[] d; delete[] e; delete[] f; delete g; delete h; delete i; j->~T(); delete k; std::printf("\n"); return 0; }
