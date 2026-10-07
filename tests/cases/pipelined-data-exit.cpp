// Loops that leave on a byte they read - strlen, strchr, strcmp, a string hash and their neighbours - which the C6000
// unrolls eight turns over one aligned 8-byte block, reading ahead only inside a block a turn has read. Every string is
// placed at all sixteen alignments, at lengths 0 to 17, 31 to 33, 64 and 257, and once ending on its buffer's last byte.
extern "C" int printf(const char *, ...);

static char area[16 + 300 + 16];
static char other[16 + 300 + 16];
static char tail[40];                  // a string that ends on the last byte of its object
static const unsigned char table[16] = { 3, 1, 4, 1, 5, 9, 2, 6, 5, 3, 5, 8, 9, 7, 9, 3 };

static unsigned lengthOf(const char *s) { const char *p = s; while (*p) p++; return (unsigned)(p - s); }
static const char *find(const char *s, int c) { while (*s && *s != (char)c) s++; return *s == (char)c ? s : 0; }
static int compare(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}
static unsigned hash(const char *p) { unsigned h = 5381; for (; *p; p++) h = h * 33 + (unsigned char)*p; return h; }
// The test reads the byte after the one it steps past, so two bytes a turn, one place apart.
static unsigned ahead(const char *p) { unsigned n = 0; while (p[0] && p[1]) { n += (unsigned char)p[0]; p++; } return n * 7 + (unsigned)(p[0] & 0x7f); }
// A post-increment in the test, the pointer left one past the terminator.
static int counted(const char *p) { int n = 0; while (*p++) n++; return n * 3 + (p[-1] == 0); }
// A signed byte read and kept: the high bit must come back sign-extended.
static int lastSigned(const char *p) { int last = -1000; while (*p != 0) { last = *p; p++; } return last; }
// A byte that indexes a table: that load is not read ahead, only the string's is.
static unsigned mixed(const char *p) { unsigned h = 0; while (*p) { h = h * 5 + table[(unsigned char)*p & 15]; p++; } return h; }
// Stops on a given byte, or on the end, whichever comes first.
static unsigned untilByte(const unsigned char *p, unsigned char c) {
    unsigned n = 0;
    while (*p != c && *p != 0) { n++; p++; }
    return n * 256 + *p;
}

// Returns from inside the loop, as RTS6x's reference strchr is written: two ways out, each setting the result.
static const char *found(const char *s, int c) {
    for (char ch = (char)c;; s++) {
        if (*s == ch) return s;
        if (!*s) return 0;
    }
}
// Two ways out to two different places, each with something left to do.
static int scan(const char *p, char stop) {
    int n = 0;
    for (;; p++) {
        if (*p == stop) break;
        if (!*p) goto end;
        n += 2;
    }
    n += 1000 + (unsigned char)p[0];
end:
    return n;
}

static void fill(char *at, int len, int seed) {
    for (int i = 0; i < len; i++) {
        int v = (i * 37 + seed * 11) % 255 + 1;           // 1..255: some above 127, never zero
        at[i] = (char)v;
    }
    at[len] = 0;
    for (int i = 1; i < 16; i++) at[len + i] = (char)(0x41 + i);   // what a read past the end would see
}

int main() {
    static const int lengths[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 31, 32, 33, 64, 257 };
    for (unsigned li = 0; li < sizeof lengths / sizeof lengths[0]; li++) {
        const int len = lengths[li];
        unsigned sum = 0, hs = 0, cs = 0, fs = 0, xs = 0;
        int ss = 0;
        for (int a = 0; a < 16; a++) {
            for (int i = 0; i < (int)sizeof area; i++) area[i] = (char)0x5a, other[i] = (char)0x5a;
            fill(area + a, len, a);
            fill(other + (a * 5 + 3) % 16, len, a);
            if (len > 0) other[(a * 5 + 3) % 16 + len / 2] ^= (char)(a & 1);    // differs half of the time
            sum += lengthOf(area + a) * (unsigned)(a + 1);
            hs ^= hash(area + a) + (unsigned)a;
            const char *f = find(area + a, len > 2 ? area[a + len / 2] : 'q');
            fs += f ? (unsigned)(f - (const char *)(area + a)) + 1 : 0;
            ss += compare(area + a, other + (a * 5 + 3) % 16) * (a + 1);
            cs += ahead(area + a) + (unsigned)counted(area + a) + (unsigned)lastSigned(area + a);
            const char *g = found(area + a, len > 4 ? area[a + len - 3] : 'q');
            xs += (g ? (unsigned)(g - (const char *)area) : 7u) + (unsigned)scan(area + a, len > 5 ? area[a + 5] : 'q');
            xs += mixed(area + a) + untilByte((const unsigned char *)area + a, (unsigned char)(len > 3 ? area[a + 3] : 1));
        }
        printf("len %3d: %u %u %u %d %u %u\n", len, sum, hs, fs, ss, cs, xs);
    }
    // Ending on the last byte of an object: a read past it would leave the array.
    for (int len = 0; len < 40; len++) {
        for (int i = 0; i < 40; i++) tail[i] = (char)0x7e;
        for (int i = 0; i < len; i++) tail[39 - len + i] = (char)('a' + i % 26);
        tail[39] = 0;
        const char *s = tail + 39 - len;
        printf("%u %u %d %u %d|", lengthOf(s), hash(s), compare(s, s), (unsigned)counted(s), lastSigned(s));
        if (len % 8 == 7) printf("\n");
    }
    printf("\n");
    return 0;
}
