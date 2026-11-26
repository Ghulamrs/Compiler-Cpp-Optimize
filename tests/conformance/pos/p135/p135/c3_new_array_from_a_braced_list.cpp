// R28: new T[n]{...} - listed elements copy-initialised, the rest value-initialised.
#include <cstdio>
#include <new>
struct T { int v; T() : v(5) {} T(int x) : v(x) {} ~T() { std::printf("~%d ", v); } };
struct P { int a, b; };
int main(int argc, char **) {
    int *f = new int[3]{1, 2, 3};
    int *g = new int[argc + 3]{7, 8};
    double *d = new double[2]{1.5};
    P *p = new P[2]{{1, 2}, {3, 4}};
    T *e = new T[2]{1, 2};
    T *h = new T[argc + 2]{9};
    std::printf("%d %d %d | %d %d %d %d | %g %g | %d %d %d %d | %d %d | %d %d %d\n", f[0], f[1], f[2],
                g[0], g[1], g[2], g[3], d[0], d[1], p[0].a, p[0].b, p[1].a, p[1].b, e[0].v, e[1].v,
                h[0].v, h[1].v, h[2].v);
    delete[] f; delete[] g; delete[] d; delete[] p; delete[] e; delete[] h;
    std::printf("\n");
    return 0;
}