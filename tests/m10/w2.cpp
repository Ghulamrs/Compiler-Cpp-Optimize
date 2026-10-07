// M10 W2: parameters, locals, globals and a static with base and pointer types.
// Gate: `m10.py oracle tests/m10/w2.cpp 20` (in the block) and `... 23` (after it):
// every name, type and value in `dv /t /V` equal to cl /Zi's, an address on each.
#include <stdio.h>

int counter = 41;
static double ratio = 2.5;

int work(int n, const char *label, double scale) {
    static int calls = 0;
    calls = calls + 1;
    char c = label[0];
    bool big = n > 10;
    double d = scale * n;
    const int k = 3;
    int *p = &counter;
    unsigned u = 7u;
    long long w = 1234567890123LL;
    {
        int inner = n + k;
        printf("%c %d %d %d\n", c, big, inner, *p);
    }
    printf("%f %u %lld %f\n", d, u, w, ratio);
    return calls;
}

int main() {
    int r = work(12, "hello", 0.5);
    return r == 1 ? 0 : 1;
}
