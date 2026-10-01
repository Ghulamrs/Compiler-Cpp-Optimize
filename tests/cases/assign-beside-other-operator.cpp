// A class that declares `operator=(const double *)` still has its implicit copy
// assignment, [class.copy]/17 - only X, X & or const X & make one a copy. Found by a
// CCS 5.5 sample (`matrix[i] = v`); TI's cl6x and clang both compile it.
extern "C" int printf(const char *, ...);

struct V {
    double a[2];
    V() { a[0] = 1; a[1] = 2; }
    V &operator=(const double *p) { a[0] = p[0]; a[1] = p[1]; return *this; }
};

// A member whose copy assignment has work to do, so the implicit one is a function.
struct Counted {
    int n;
    Counted() : n(0) { }
    Counted &operator=(const Counted &o) { n = o.n + 100; return *this; }
};
struct W {
    Counted c;
    int k;
    W &operator=(int v) { k = v; return *this; }
};

int main() {
    V x, y;
    double d[2] = {5, 6};
    y = d;
    x = y;
    V arr[2];
    for (int i = 0; i < 2; i++) arr[i] = x;
    W p, q;
    q = 7;
    q.c.n = 3;
    p = q;
    printf("%g %g %g %g %d %d\n", x.a[0], x.a[1], arr[1].a[0], arr[1].a[1], p.k, p.c.n);
    return 0;
}
