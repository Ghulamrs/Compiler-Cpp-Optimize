// **Loop-invariant code motion after allocation** (-O2): a computation of
// registers the loop never writes moves in front of the loop. Each shape
// below is one the pass must hoist, must refuse, or must un-plan; the
// printed values are clang's, and a wrong register at any of them shows.
extern "C" int printf(const char *, ...);

static int sv[64];
static double ma[8][8], mb[8][8], mc[8][8];
static char comp[100];
static unsigned seed = 7;

// A global's address inside a loop, and the loop's own index beside it.
int sumArray(int n) {
    int s = 0;
    for (int i = 0; i < n; ++i) s += sv[i];
    return s;
}

// An inner loop whose row addresses depend only on the outer counters:
// two chains hoisted, one of them computed twice in the body (shared).
double rows(int n) {
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j) { ma[i][j] = i + j; mb[i][j] = i * j; mc[i][j] = 0; }
    for (int i = 0; i < n; ++i)
        for (int k = 0; k < n; ++k) {
            double x = ma[i][k];
            for (int j = 0; j < n; ++j) mc[i][j] += x * mb[k][j];
        }
    double s = 0;
    for (int i = 0; i < n; ++i) for (int j = 0; j < n; ++j) s += mc[i][j] * (i + 1);
    return s;
}

// A bound sign-extended every turn, and a stride that is the outer index.
int strides(int n) {
    for (int i = 0; i <= n; ++i) comp[i] = 0;
    int count = 0;
    for (int i = 2; i <= n; ++i) {
        if (comp[i]) continue;
        ++count;
        for (long long j = (long long)i * i; j <= n; j += i) comp[j] = 1;
    }
    return count;
}

// A divisor the loop never writes: `div` reads its operand by name and
// rax and rdx unnamed, so the divisor may move and the dividend may not.
unsigned divides(int n, unsigned d) {
    unsigned s = 0;
    for (int i = 0; i < n; ++i) { seed = seed * 1103515245u + 12345u; s += (seed >> 8) % d; }
    return s;
}

// A shift whose count is the invariant: %cl cannot be renamed.
unsigned shifts(int n, int by) {
    unsigned s = 0;
    for (int i = 0; i < n; ++i) s += ((unsigned)i << by) ^ (s >> by);
    return s;
}

int twice(int x) { return x * 2; }
// A call in the loop, through a pointer so that nothing inlines it: every
// caller-saved register is written there, and nothing moves.
int called(int n, int k, int (*f)(int)) {
    int s = 0;
    for (int i = 0; i < n; ++i) s += f(sv[i] + k * 3);
    return s;
}

// Two blocks in the body reading the same invariant address, and an
// `if` whose arms both write through it.
int branches(int n, int k) {
    int s = 0;
    for (int i = 0; i < n; ++i) {
        if (sv[i] > k) sv[i] -= k;
        else sv[i] += k;
        s += sv[i];
    }
    return s;
}

// A do-while: the head is the body, entered by falling through.
int doWhile(int n, int k) {
    int s = 0, i = 0;
    do { s += sv[i] * k; ++i; } while (i < n);
    return s;
}

// A loop never entered: what moves in front of it must be harmless.
int empty(int n, int k) {
    int s = 1;
    for (int i = 0; i < n; ++i) s += sv[i] * k;
    return s;
}

// More invariant rows than caller-saved registers: the inner loop takes
// what it can, and a block's plan the registers cannot carry is dropped whole.
double crowded(int n) {
    double s = 0;
    for (int i = 0; i < n; ++i)
        for (int k = 0; k < n; ++k)
            for (int j = 0; j < n; ++j) s += ma[i][j] * mb[k][j] + mc[i][j] * ma[k][j] + mb[i][j] * mc[k][j];
    return s;
}

int main() {
    for (int i = 0; i < 64; ++i) sv[i] = (i * 37) % 23 - 11;
    printf("sum %d\n", sumArray(64));
    printf("rows %.0f\n", rows(8));
    printf("strides %d\n", strides(99));
    printf("divides %u\n", divides(50, 1000u));
    printf("shifts %u\n", shifts(40, 3));
    printf("called %d\n", called(64, 5, twice));
    printf("branches %d\n", branches(64, 4));
    printf("do %d\n", doWhile(64, 3));
    printf("empty %d %d\n", empty(0, 9), empty(5, 9));
    printf("crowded %.0f\n", crowded(8));
    return 0;
}
