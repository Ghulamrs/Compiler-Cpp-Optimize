// **A 64-bit shift by a count the compiler cannot see, either side of 32.**
// The tms6747 backend moves a word across for a count of 32 or more, and
// that path read the count as 32 whatever it was; no case had such a count.
extern "C" int printf(const char *, ...);

static int count(int n) { return n; }

int main() {
    long long a = 1, s = -1LL << 60;
    unsigned long long u = 0x8000000000000000ULL;
    for (int i = 0; i < 64; i += 7) {
        int n = count(i);
        printf("%d: %lld %llu %lld %llu\n", n, a << n, u >> n, s >> n, (u | 5) >> n);
    }
    printf("%lld %lld %llu\n", (a << count(40)) >> count(8),
                (s >> count(33)) << count(31), (u >> count(63)) << count(32));
    return 0;
}
