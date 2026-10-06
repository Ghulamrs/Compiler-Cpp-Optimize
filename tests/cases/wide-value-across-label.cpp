// A 64-bit value chosen by a branch - `if (v < 0) m = 0ull - m;` and `?:` - where RTS6x's signed
// 64-bit division left its end label referenced and undefined on the C6000 at -O2: a pair copy
// that stopped at the label took the label for its partner copy and removed it. Both arms run.
extern "C" int printf(const char *, ...);

class WideDivision {
public:
    static unsigned long long divide(unsigned long long n, unsigned long long d, unsigned long long &remainder);
    static long long divide(long long n, long long d, long long &remainder);
private:
    static unsigned long long magnitude(long long v) { unsigned long long m = (unsigned long long)v; if (v < 0) m = 0ull - m; return m; }
};

unsigned long long WideDivision::divide(unsigned long long n, unsigned long long d, unsigned long long &remainder) {
    unsigned long long q = 0, r = 0;
    for (int i = 63; i >= 0; i--) {
        r = (r << 1) | ((n >> i) & 1u);
        q <<= 1;
        if (r >= d) { r -= d; q |= 1u; }
    }
    remainder = r;
    return q;
}

long long WideDivision::divide(long long n, long long d, long long &remainder) {
    unsigned long long r;
    unsigned long long q = divide(magnitude(n), magnitude(d), r);
    remainder = n < 0 ? -(long long)r : (long long)r;
    return (n < 0) != (d < 0) ? -(long long)q : (long long)q;
}

extern "C" long long pick(long long n, unsigned long long r) {
    long long out = n < 0 ? (long long)(0ull - r) : (long long)r;
    return out;
}
extern "C" unsigned long long pickUnsigned(int c, unsigned long long a, unsigned long long b) {
    unsigned long long out = c ? a + 1 : b - 1;
    return out;
}
extern "C" double pickDouble(int c, double a, double b) {
    double out = c ? a * 2.0 : b / 2.0;
    return out;
}

int main() {
    const long long ns[4] = { 100000000007LL, -100000000007LL, 7, -7 };
    const long long ds[4] = { 3, -3, -2, 2 };
    for (int i = 0; i < 4; i++) {
        long long rem;
        const long long q = WideDivision::divide(ns[i], ds[i], rem);
        printf("%lld %lld\n", q, rem);
    }
    printf("%lld %lld\n", pick(-1, 5000000000ULL), pick(1, 5000000000ULL));
    printf("%llu %llu\n", pickUnsigned(1, 0xffffffffULL, 9), pickUnsigned(0, 9, 0x100000000ULL));
    printf("%g %g\n", pickDouble(1, 1.5, 9.0), pickDouble(0, 1.5, 9.0));
    return 0;
}
