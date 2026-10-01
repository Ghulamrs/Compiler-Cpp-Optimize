// Counted loops whose addresses the C6000 optimizer turns into pointers the accesses step themselves (`*P++(k)`),
// whose pair results are made where the backend copied them to, and whose loads are scheduled late in the kernel -
// each run at trip counts 0, 1, 2, small, odd and large, so a pointer started one stride off, a copy of a renamed pair
// missed at entry or exit, or an access scheduled past its reader shows as a wrong sum.
extern "C" int printf(const char *, ...);

static double da[300], db[300], dc[300];
static int ia[300], ib[300];
static short ha[300];
static unsigned char ca[300];

// A double multiply-add through three arrays: two loads and a store, every address the counter shifted by three.
static double axpy(int n, double x) {
    int i;
    double s = 0;
    for (i = 0; i < n; ++i) dc[i] += x * db[i];
    for (i = 0; i < n; ++i) s += dc[i];
    return s;
}

// A loop-carried double sum - the recurrence through ADDDP, its pair read and written across the register files.
static double sumd(int n) {
    int i;
    double s = 1.5;
    for (i = 0; i < n; ++i) s += da[i];
    return s;
}

// A byte store from the counter with no shift, the address being base plus the counter itself.
static int fillb(int n) {
    int i, s = 0;
    for (i = 0; i <= n; ++i) ca[i] = (unsigned char)(i * 5 + 1);
    for (i = 0; i <= n; ++i) s = s * 3 + ca[i];
    return s;
}

// Shorts and ints, shifted by one and two: the same pointer rewrite at the other widths, with a load and a store each.
static int scale(int n) {
    int i, s = 0;
    for (i = 0; i < n; ++i) ha[i] = (short)(ib[i] * 3 - 7);
    for (i = 0; i < n; ++i) ia[i] = ha[i] + ib[i];
    for (i = 0; i < n; ++i) s += ia[i] ^ (i << 2);
    return s;
}

// The counter read after the loop, and the array written walked backwards afterwards by an ordinary loop.
static int tail(int n) {
    int i, s = 0;
    for (i = 0; i < n; ++i) ib[i] = i * i - n;
    for (i = n; i-- > 0;) s += ib[i] * (i & 1 ? 1 : -1);
    return s + i;
}

int main() {
    static const int counts[] = { 0, 1, 2, 3, 4, 5, 7, 8, 9, 15, 16, 17, 31, 33, 64, 100, 257 };
    int k, i;
    for (i = 0; i < 300; ++i) { da[i] = i * 0.5 - 20; db[i] = (i % 7) - 3; dc[i] = 0.25 * i; ib[i] = i * 11 - 500; }
    for (k = 0; k < 17; ++k) {
        int n = counts[k];
        printf("%d: %.2f %.2f %d %d %d\n", n, axpy(n, 1.5), sumd(n), fillb(n), scale(n), tail(n));
    }
    return 0;
}
