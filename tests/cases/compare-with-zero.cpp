// Every comparison against zero the x86 optimizer may write as `test %r, %r` in place of `cmp $0, %r`:
// the six relations on signed and unsigned values of four widths, a pointer, and a loop counting down to it.
// The flags the two leave are the same, so every answer must be too.
extern "C" int printf(const char *, ...);

static int relations(long long v) {
    int r = 0;
    if (v == 0) r |= 1;
    if (v != 0) r |= 2;
    if (v < 0) r |= 4;
    if (v <= 0) r |= 8;
    if (v > 0) r |= 16;
    if (v >= 0) r |= 32;
    return r;
}

static int narrow(int i, short s, signed char c) {
    int r = 0;
    if (i < 0) r += 1;
    if (s > 0) r += 10;
    if (c <= 0) r += 100;
    if (i >= 0 && s != 0) r += 1000;
    return r;
}

static int unsignedRelations(unsigned u, unsigned long long w) {
    int r = 0;
    if (u > 0) r |= 1;
    if (u == 0) r |= 2;
    if (w != 0) r |= 4;
    if (w > 0u) r |= 8;
    return r;
}

static int pointers(const int *p, const int *q) {
    return (p ? 1 : 0) + (q == 0 ? 10 : 0) + (!p ? 100 : 0);
}

static int countDown(int n) {
    int s = 0;
    while (n != 0) { s += n; --n; }
    for (int k = 5; k > 0; --k) s += k * 3;
    return s;
}

int main() {
    long long values[] = { 0, 1, -1, 7, -7, 2147483647LL, -2147483647LL - 1, 4294967296LL, -4294967296LL };
    for (int i = 0; i < 9; ++i) printf("%d ", relations(values[i]));
    printf("\n");
    printf("%d %d %d %d\n", narrow(-3, 4, 0), narrow(5, -2, 9), narrow(0, 0, -1), narrow(2147483647, 1, 127));
    printf("%d %d %d %d\n", unsignedRelations(0, 0), unsignedRelations(1, 1), unsignedRelations(4294967295u, 0),
           unsignedRelations(0, 18446744073709551615ull));
    int n = 3;
    printf("%d %d\n", pointers(&n, nullptr), pointers(nullptr, &n));
    printf("%d %d %d\n", countDown(0), countDown(4), countDown(100));
    return 0;
}
