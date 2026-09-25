// **Arrays of a class with a destructor**: a local one destroyed last first
// when its scope ends, by return as by falling off; `new T[n]` building every
// element by the class's loop with the count kept in a cookie in front,
// and `delete[]` reading it back, destroying last first and freeing from
// where the allocation began; a null delete[]; the trivial forms as before.
// What is not built: unwinding out of a constructor mid-array. Refused by
// name until 2026-09-14 (review A7).
extern "C" int printf(const char *, ...);
static int built = 0, gone = 0, last = -1;
struct T {
    int id;
    T() : id(built++) {}
    ~T() { gone++; last = id; }
};
struct D8 { double d; ~D8() { gone += 100; } };
int scope(int k) {
    T a[3];
    if (k == 1) return a[1].id;
    {
        T b[2];
        printf("in %d %d %d\n", built, b[1].id, gone);
    }
    printf("out %d %d %d\n", built, gone, last);
    return a[2].id;
}
int main() {
    printf("%d %d\n", scope(0), scope(1));
    printf("%d %d %d\n", built, gone, last);
    int n = 4;
    T *p = new T[n];
    printf("new %d %d %d %d\n", built, p[0].id, p[3].id, gone);
    delete[] p;
    printf("del %d %d %d\n", built, gone, last);
    D8 *q = new D8[2];
    q[1].d = 2.5;
    printf("%d %d\n", (int)q[1].d, (int)(((unsigned long long)q % 8) == 0));
    delete[] q;
    printf("%d\n", gone);
    T *none = 0;
    delete[] none;
    int *ints = new int[n];
    ints[3] = 7;
    delete[] ints;
    printf("%d %d\n", gone, n);
    return 0;
}
