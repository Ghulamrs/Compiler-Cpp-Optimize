// Counted loops of the shapes the C6000 software pipeliner rewrites at -O2, each run at trip counts 0, 1, 2,
// just below and above the pipeline's depth, and large - so a prologue or epilogue that runs an iteration too
// many or too few, or a copy of a renamed register missed at entry or exit, shows as a wrong sum.
extern "C" int printf(const char *, ...);

static unsigned char buf[300];
static int a[300], b[300];

// A store per turn: the value written and the address both from the counter, and the counter read after.
static int fill(int n) {
    int i;
    for (i = 0; i <= n; ++i) buf[i] = (unsigned char)(i * 3);
    return i;
}

// A loop-carried scalar: h from the previous turn, a load, a multiply.
static unsigned mix(int n) {
    unsigned h = 7, i;
    for (i = 0; i < (unsigned)n; ++i) h = (h ^ buf[i]) * 16777619u;
    return h;
}

// A memory recurrence, a[i] = a[i-1] + x: the store of one turn is the load of the next.
static int chain(int n, int x) {
    int i;
    a[0] = 1;
    for (i = 1; i < n; ++i) a[i] = a[i - 1] + x;
    return a[n > 0 ? n - 1 : 0];
}

// Two arrays, one read and one written, and a sum kept across the loop.
static int gather(int n) {
    int i, s = 0;
    for (i = 0; i < n; ++i) { b[i] = a[i] * 2 + i; s += b[i]; }
    return s;
}

// The counter used after the loop, and a bound that is a register the loop reads.
static int upto(int lo, int hi) {
    int i, s = 0;
    for (i = lo; i < hi; ++i) s += i;
    return s * 10 + (i - hi);
}

int main() {
    static const int counts[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 15, 16, 17, 31, 32, 33, 64, 100, 257 };
    int k;
    for (k = 0; k < 19; ++k) {
        int n = counts[k];
        int f = fill(n);
        unsigned h = mix(n);
        int c = chain(n, 5), g = gather(n), u = upto(3, n);
        printf("%d: %d %u %d %d %d\n", n, f, h, c, g, u);
    }
    return 0;
}
