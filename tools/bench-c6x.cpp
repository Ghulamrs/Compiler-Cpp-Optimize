// The six kernels of tools/windows/bench-kernels.cpp cut to sizes the VM6747
// emulator runs in seconds, timed in kilocycles: its clock() is cycles/1000
// and a library call costs it nothing, so the numbers are the C6000 code's own.
// The last line is a checksum every optimisation level must print the same.
#include <stdio.h>
#include <time.h>

static unsigned seed = 12345;
unsigned rnd() { seed = seed * 1103515245u + 12345u; return seed >> 8; }

int fib(int n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }

static char comp[20001];
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

static double ma[24][24], mb[24][24], mc[24][24];
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

static int sv[600];
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
void stop(const char *name) { printf("%s %ld\n", name, (long)(clock() - t0)); }

int main() {
    clock_t total = clock();
    start(); int f = fib(20); stop("fib");
    start(); int p = 0; for (int r = 0; r < 2; ++r) p += sieve(20000); stop("sieve");
    start(); double m = matmul(24); stop("matmul");
    start(); long long q = isort(600); stop("isort");
    start(); unsigned h = hashes(2000); stop("hash");
    start(); long long v = virt(20000); stop("virtual");
    printf("total %ld\n", (long)(clock() - total));
    printf("check %d %d %.0f %lld %u %lld\n", f, p, m, q, h, v);
    return 0;
}
