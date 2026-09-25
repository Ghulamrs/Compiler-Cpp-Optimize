// **Three rewrites in forward-values** (-O1 and -O2): `mov $c, %d; add %s,
// %d` is `lea c(%s), %d` where nothing reads the add's flags; a sign
// extension another register already holds is a copy of it; and a byte or
// word extension read only at its source's width is a copy, which then
// goes. Each shape here has a neighbour the rewrite must leave alone, and
// the values are clang's.
extern "C" int printf(const char *, ...);

static char buf[64];
static short wide[16];
static long long sums[16];

// A byte store of an int expression: the extension to the store is dead.
void fill(int r) {
    for (int i = 0; i < 63; ++i) buf[i] = (char)('a' + (r + i) % 26);
    buf[63] = 0;
}

// The same extension read wide afterwards: it stays.
long long keep(int x) {
    char c = (char)(x * 3 + 7);
    long long l = c;              // the sign matters here
    buf[0] = c;
    return l * 1000 + buf[0];
}

// A word extension, stored narrow and, in the second half, used as an index (wide).
int words(int x) {
    short s = (short)(x * 5 + 1);
    wide[1] = s;
    short t = (short)(x + 2);
    return wide[1] + sums[t & 7] + t;
}

// One int extended twice for two subscripts: the second extension is a copy.
long long twice(int i, int j) {
    sums[i] = j;
    return sums[i] + sums[j] * (long long)i;
}

// A constant plus a register whose flags are then read: no lea.
int flagged(int x, int y) {
    int t = 100 + x;
    if (t == y) return 1;
    int u = -50 + y;
    return u < x ? u : t;
}

// And a sum the flags of which nobody reads, at four and eight bytes.
long long plain(int x, long long y) {
    int a = 97 + x;
    long long b = 1000000 + y;
    return a * 3 + b;
}

int main() {
    fill(3);
    printf("%s\n", buf);
    printf("%lld %lld\n", keep(50), keep(-70));
    printf("%d %d\n", words(7), words(-9));
    for (int i = 0; i < 16; ++i) sums[i] = i * i;
    printf("%lld %lld\n", twice(3, 5), twice(9, 2));
    printf("%d %d %d\n", flagged(1, 101), flagged(60, 20), flagged(-100, -60));
    printf("%lld %lld\n", plain(3, 4), plain(-200, -3000000));
    return 0;
}
