// Six small kernels timed one by one, for comparing cxx1's -O2 with cl /O2,
// g++ -O2 and cl6x --opt_level=2. Each prints its time in milliseconds; the
// last line is a checksum that every compiler must print the same.
#include <stdio.h>
#include <time.h>

static unsigned seed = 12345;
unsigned rnd() { seed = seed * 1103515245u + 12345u; return seed >> 8; }

int fib(int n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }

static char comp[2000001];
int sieve(int n) {
    for (int i = 0; i <= n; ++i) comp[i] = 0;
    int count = 0;
    for (int i = 2; i <= n; ++i) {
        if (comp[i]) continue;
        ++count;
        for (long long j = (long long)i * i; j <= n; j += i) comp[j] = 1;
    }
    return count;
}

static double ma[160][160], mb[160][160], mc[160][160];
double matmul(int n) {
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j) { ma[i][j] = (i + j) % 7; mb[i][j] = (i * j) % 5; mc[i][j] = 0; }
    for (int i = 0; i < n; ++i)
        for (int k = 0; k < n; ++k) {
            double x = ma[i][k];
            for (int j = 0; j < n; ++j) mc[i][j] += x * mb[k][j];
        }
    double s = 0;
    for (int i = 0; i < n; ++i) for (int j = 0; j < n; ++j) s += mc[i][j];
    return s;
}

static int sv[6000];
long long isort(int n) {
    for (int i = 0; i < n; ++i) sv[i] = (int)(rnd() % 100000);
    for (int i = 1; i < n; ++i) {
        int x = sv[i], j = i - 1;
        while (j >= 0 && sv[j] > x) { sv[j + 1] = sv[j]; --j; }
        sv[j + 1] = x;
    }
    long long s = 0;
    for (int i = 0; i < n; i += 97) s += sv[i];
    return s;
}

unsigned hashes(int rounds) {
    char buf[64];
    unsigned h = 0;
    for (int r = 0; r < rounds; ++r) {
        for (int i = 0; i < 63; ++i) buf[i] = (char)('a' + (r + i) % 26);
        buf[63] = 0;
        unsigned x = 2166136261u;
        for (const char *p = buf; *p; ++p) x = (x ^ (unsigned char)*p) * 16777619u;
        h ^= x;
    }
    return h;
}

struct Shape { virtual ~Shape() {} virtual int area() const = 0; };
struct Sq : Shape { int s; Sq(int x) : s(x) {} int area() const { return s * s; } };
struct Rc : Shape { int w, h; Rc(int a, int b) : w(a), h(b) {} int area() const { return w * h; } };
long long virt(int n) {
    Sq a(3);
    Rc b(4, 5);
    Shape *p[2] = {&a, &b};
    long long s = 0;
    for (int i = 0; i < n; ++i) s += p[i & 1]->area();
    return s;
}

static clock_t t0;
void start() { t0 = clock(); }
long ms(clock_t from) { return (long)((double)(clock() - from) * 1000.0 / CLOCKS_PER_SEC); }
void stop(const char *name) { printf("%s %ld\n", name, ms(t0)); }

int main() {
    clock_t total = clock();
    start(); int f = fib(32); stop("fib");
    start(); int p = 0; for (int r = 0; r < 10; ++r) p += sieve(2000000); stop("sieve");
    start(); double m = 0; for (int r = 0; r < 5; ++r) m += matmul(160); stop("matmul");
    start(); long long q = 0; for (int r = 0; r < 3; ++r) q += isort(6000); stop("isort");
    start(); unsigned h = hashes(2000000); stop("hash");
    start(); long long v = virt(100000000); stop("virtual");
    printf("total %ld\n", ms(total));
    printf("check %d %d %.0f %lld %u %lld\n", f, p, m, q, h, v);
    return 0;
}
