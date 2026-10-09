// A range-based `for` over a class with no `begin` or `end` member -
// [stmt.ranged]/1 falls back to `begin(r)` and `end(r)` found by
// argument-dependent lookup, in the class's own namespace or the global one.
// Refused by name until now.
extern "C" int printf(const char *, ...);

namespace shelf {
    struct Row { int cells[3]; };
    int *begin(Row &r) { return r.cells; }
    int *end(Row &r) { return r.cells + 3; }
    const int *begin(const Row &r) { return r.cells; }
    const int *end(const Row &r) { return r.cells + 3; }
}

struct Pair { double a[2]; };
double *begin(Pair &p) { return p.a; }
double *end(Pair &p) { return p.a + 2; }

int main(void) {
    shelf::Row r = { { 4, 5, 6 } };
    for (int x : r) printf("%d ", x);
    printf("\n");
    for (int &x : r) x += 1;
    const shelf::Row &cr = r;
    for (int x : cr) printf("%d ", x);
    printf("\n");
    Pair p = { { 1.5, 2.5 } };
    for (double v : p) printf("%.1f ", v);
    printf("\n");
    return 0;
}
