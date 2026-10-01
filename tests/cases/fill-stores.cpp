// Loops that fill memory with one value, which the C6000 optimizer stores as doublewords past fifteen elements: bytes,
// shorts and words, from every starting alignment, at counts of 0, 1, 2, small, exactly the threshold, odd and large,
// a value whose high bits must not reach the array, and the counter and the pointer read after the loop. Every byte
// outside the filled run is checked untouched.
extern "C" int printf(const char *, ...);

static unsigned char bytes[600];
static short shorts[300];
static int words[300];

static int fillBytes(int from, int n, int value) {
    int i;
    for (i = from; i < from + n; ++i) bytes[i] = (unsigned char)value;
    return i;
}

static int fillShorts(int from, int n, short value) {
    int i;
    for (i = from; i <= from + n - 1; ++i) shorts[i] = value;
    return i;
}

static int fillWords(int from, int n, int value) {
    int i;
    for (i = from; i < from + n; ++i) words[i] = value;
    return i;
}

static unsigned sum(void) {
    unsigned s = 0, i;
    for (i = 0; i < 600; ++i) s = s * 31 + bytes[i];
    for (i = 0; i < 300; ++i) s = s * 31 + (unsigned short)shorts[i];
    for (i = 0; i < 300; ++i) s = s * 31 + (unsigned)words[i];
    return s;
}

// At file scope: a static local named in `main` has no Itanium name here yet (`main.counts`), which names.sh would report.
static const int counts[] = { 0, 1, 2, 3, 7, 8, 9, 15, 16, 17, 23, 24, 25, 31, 33, 64, 100, 257 };

int main() {
    int k, a, r = 0;
    for (k = 0; k < 18; ++k)
        for (a = 0; a < 8; ++a) {
            int n = counts[k];
            r += fillBytes(a + 3, n, 0x1234 + k + a);
            r += fillShorts(a + 2, n, (short)(0x7f01 + 3 * k - a));
            r += fillWords(a + 1, n, 0x12345678 + 11 * k - a);
            printf("%d %d: %u %d\n", n, a, sum(), r);
        }
    return 0;
}
