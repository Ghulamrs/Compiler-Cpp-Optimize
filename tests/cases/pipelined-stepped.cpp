// Counted loops whose counter steps by more than one - a register, a constant, one word or two - which the C6000
// pipeliner runs with the loop's own test against a bound moved back by the turns in flight, never a turn the loop
// would not have run. Each at trip counts 0, 1, 2, small, odd and large, with the counter read after the loop, and
// one whose entry guard must decline at small counts: a bound within the moved steps of INT_MIN.
extern "C" int printf(const char *, ...);

static unsigned char mark[2000];

// The sieve's inner loop: a 64-bit counter stepped by a register, running while it is at most n.
static long long wide(int n, int s) {
    long long j, c = 0;
    for (j = (long long)s * s; j <= n; j += s) { mark[j] += 1; c += j; }
    return c * 1000 + j;
}

// One word, a register step, at most n.
static int narrowLe(int n, int s) {
    int j, c = 0;
    for (j = s; j <= n; j += s) { mark[j] += 2; c += j; }
    return c * 100 + j;
}

// One word, a register step, below n.
static int narrowLt(int n, int s) {
    int j, c = 0;
    for (j = 1; j < n; j += s) { mark[j] += 4; c ^= j; }
    return c * 100 + j;
}

// A constant step, below n.
static int constant(int n) {
    int j, c = 0;
    for (j = 2; j < n; j += 3) { mark[j] += 8; c += j * 3; }
    return c * 100 + j;
}

// A constant step on two words, which the backend carries in a register pair.
static long long wideConstant(int n) {
    long long j, c = 0;
    for (j = 3; j <= n; j += 5) { mark[j] += 16; c += j; }
    return c * 100 + j;
}

// A bound a few steps above INT_MIN: moving it back would overflow, so the guard runs the loop as written.
static int nearMin(int turns) {
    int j, c = 0, n = -2147483647 - 1 + 2 * turns;
    for (j = -2147483647 - 1 + 2; j <= n; j += 2) { c += (j & 6) + 1; c ^= j >> 3; }
    return c * 10 + (j - n);
}

// Two words, a register step, below n.
static long long wideLt(int n, int s) {
    long long j, c = 0;
    for (j = s; j < n; j += s) { mark[j] += 32; c += j; }
    return c * 100 + j;
}

// At file scope: a static local named in `main` has no Itanium name here yet (`main.counts`), which names.sh would report.
static const int counts[] = { 0, 1, 2, 3, 4, 5, 7, 8, 9, 15, 16, 17, 31, 33, 64, 100, 257 };

int main() {
    int k, i, sum = 0;
    for (k = 0; k < 17; ++k) {
        int t = counts[k];
        // Bounds chosen so that each loop runs t turns at its step.
        printf("%d: %lld %d %d %d %lld %d %lld\n", t, wide(7 * (t + 6), 7), narrowLe(3 * t, 3), narrowLt(1 + 5 * t, 5), constant(2 + 3 * t),
               wideConstant(3 + 5 * (t - 1) + (t ? 0 : -1)), nearMin(t), wideLt(6 * t + 1, 6));
    }
    for (i = 0; i < 2000; ++i) sum += mark[i] * (i % 13 + 1);
    printf("marks %d\n", sum);
    return 0;
}
