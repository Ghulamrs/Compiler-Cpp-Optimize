// **A switch on a 64-bit value and a bit-field in a 64-bit unit**, the two
// shapes the tms6747 backend refused by name until 2026-09-14: the cases
// compare both halves; the field is read by shifting the pair and written
// by clearing a half at a time. Every target runs this against clang.
extern "C" int printf(const char *, ...);
struct BF {
    unsigned long long a : 5;      /* low half */
    unsigned long long b : 40;     /* spans both halves */
    long long c : 19;              /* high half, signed */
};
struct BF2 { long long x : 33; unsigned long long y : 31; };
static int classify(long long v) {
    switch (v) {
    case 0: return 1;
    case -1: return 2;
    case 1LL << 40: return 3;
    case (1LL << 40) + 1: return 4;
    case -(1LL << 35): return 5;
    case 0x123456789LL:
        switch (v >> 32) { case 1: return 6; default: return 7; }
    default: return 8;
    }
}
static int count(long long v) { return v; }
int main(void) {
    BF f;
    BF2 g;
    long long probe[] = { 0, -1, 1LL << 40, (1LL << 40) + 1, -(1LL << 35), 0x123456789LL, 0x100000000LL, 5 };
    int i;
    for (i = 0; i < 8; i++) printf("%d ", classify(probe[i]));
    printf("\n");
    f.a = 29; f.b = 0xABCDEF1234ULL; f.c = -12345;
    printf("%llu %llu %lld %d\n", (unsigned long long)f.a, (unsigned long long)f.b, (long long)f.c, (int)sizeof f);
    f.b = f.b + 1; f.a = f.a + 100; f.c = f.c * 2;
    printf("%llu %llu %lld\n", (unsigned long long)f.a, (unsigned long long)f.b, (long long)f.c);
    g.x = -(1LL << 32) + 7; g.y = 0x7FFFFFFF;
    printf("%lld %llu ", (long long)g.x, (unsigned long long)g.y);
    printf("%lld\n", (long long)(g.x = 3));    /* the assignment's value, sequenced after the reads */
    printf("%d %d\n", count(f.a), (int)(f.c < 0));
    return 0;
}
