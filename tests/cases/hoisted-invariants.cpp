// Loop-invariant values the C6000 optimizer moves into a preheader at -O2: constants, addresses of globals, and
// products of registers the loop never writes - some in registers the loop reuses for other values, some read
// again after the loop, and one loop that runs no turn at all, whose preheader must then not run either.
extern "C" int printf(const char *, ...);

static int a[64], b[64];
static double m[8][8];

// The same base recomputed for a load and a store: one address once the invariant is shared.
static int scale(int n, int k) {
    int i;
    for (i = 0; i < n; ++i) b[i] = a[i] * k + 1000;
    return b[n > 0 ? n - 1 : 0];
}

// A row's base address, a product of the outer index, invariant in the inner loop.
static double rowsum(int r, int n) {
    int j;
    double s = 0;
    for (j = 0; j < n; ++j) s += m[r][j] * 2.5;
    return s;
}

// A constant whose register the loop also uses as a temporary, and a value read after the loop.
static int mixed(int n) {
    int i, t = 12345, s = 0;
    for (i = 0; i < n; ++i) { s += (a[i] ^ t) & 255; t = t * 3 + 1; }
    return s + t;
}

// The counter starts past the bound: no turn, and nothing before the loop may have moved.
static int none(int lo, int hi) {
    int i, s = 7;
    for (i = lo; i < hi; ++i) s = s * 1103515245 + a[i & 63];
    return s;
}

int main() {
    int i, r;
    for (i = 0; i < 64; ++i) a[i] = i * i - 40;
    for (i = 0; i < 64; ++i) m[i / 8][i % 8] = i * 0.5;
    for (r = 0; r < 8; ++r) printf("%d %d %.1f %d %d\n", r, scale(r * 7 + 1, r + 2), rowsum(r, 8), mixed(r * 9), none(r + 3, 3));
    printf("%d %d\n", scale(0, 5), mixed(64));
    return 0;
}
