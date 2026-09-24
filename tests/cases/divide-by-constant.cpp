// Division and remainder by a constant, signed and unsigned, at the values
// where a magic-number multiply is most likely to be off by one: the ends
// of the range, the neighbours of each multiple of the divisor, and a
// spread in between. One function per divisor, so that each stays small.
// Every line is what clang prints.
extern "C" int printf(const char *, ...);

static unsigned h;
static void note(long long v) { h = (h ^ (unsigned)v) * 16777619u + (unsigned)(v >> 32); }

// At file scope: a local array's initialiser is a memcpy for clang and stores here.
static int xs[] = { 0, 1, -1, 2, -2, 3, 7, -7, 25, 26, 27, -25, -26, -27, 99, 100, 101, 1000, -1000, 12345, -12345,
                    65535, 65536, -65536, 1000000, -1000000, 2147483647, -2147483647, -2147483647 - 1, 1073741824,
                    -1073741824, 715827882, -715827883, 1431655765, 0x7ffffffe, 123456789, -987654321 };
enum { n = sizeof xs / sizeof xs[0] };   // an enumerator: a folded const int has no symbol for clang

#define SIGNED(D) static void sig_##D() { \
    for (int i = 0; i < n; ++i) { int x = xs[i]; note(x / D); note(x % D); } \
    for (int m = -30; m <= 30; ++m) { long long p = (long long)m * D; if (p > 2147483646LL || p < -2147483647LL) continue; \
        int x = (int)p; note((x - 1) / D); note((x - 1) % D); note(x / D); note(x % D); note((x + 1) / D); note((x + 1) % D); } }
#define UNSIGNED(D) static void uns_##D() { \
    for (int i = 0; i < n; ++i) { unsigned x = (unsigned)xs[i]; note(x / D##u); note(x % D##u); } \
    for (unsigned m = 0; m <= 60; ++m) { unsigned x = m * D##u; \
        note((x - 1) / D##u); note((x - 1) % D##u); note(x / D##u); note(x % D##u); note((x + 1) / D##u); note((x + 1) % D##u); } }

SIGNED(2) SIGNED(3) SIGNED(5) SIGNED(6) SIGNED(7) SIGNED(9) SIGNED(10) SIGNED(11) SIGNED(12) SIGNED(13)
SIGNED(25) SIGNED(26) SIGNED(60) SIGNED(100) SIGNED(125) SIGNED(641) SIGNED(1000) SIGNED(4096) SIGNED(65537)
SIGNED(100000) SIGNED(1000003) SIGNED(715827883) SIGNED(1073741823) SIGNED(2147483647)
UNSIGNED(2) UNSIGNED(3) UNSIGNED(5) UNSIGNED(7) UNSIGNED(10) UNSIGNED(26) UNSIGNED(100) UNSIGNED(641)
UNSIGNED(1000) UNSIGNED(65537) UNSIGNED(100000) UNSIGNED(1000003) UNSIGNED(715827883) UNSIGNED(2147483647)

int main() {
    h = 2166136261u;
    sig_2(); sig_3(); sig_5(); sig_6(); sig_7(); sig_9(); sig_10(); sig_11(); sig_12(); sig_13(); sig_25(); sig_26();
    sig_60(); sig_100(); sig_125(); sig_641(); sig_1000(); sig_4096(); sig_65537(); sig_100000(); sig_1000003();
    sig_715827883(); sig_1073741823(); sig_2147483647();
    printf("signed %u\n", h);
    h = 2166136261u;
    uns_2(); uns_3(); uns_5(); uns_7(); uns_10(); uns_26(); uns_100(); uns_641(); uns_1000(); uns_65537();
    uns_100000(); uns_1000003(); uns_715827883(); uns_2147483647();
    printf("unsigned %u\n", h);
    // The shapes a loop writes: the quotient alone, the remainder alone, signed and unsigned.
    long long q = 0, r = 0;
    for (int i = -50000; i < 50000; i += 7) { q += i / 26; r += i % 26; q += (unsigned)i / 26u; r += (unsigned)i % 26u; }
    printf("loop %lld %lld\n", q, r);
    return 0;
}
