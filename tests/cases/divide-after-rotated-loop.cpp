// `i / 8` and `i % 8` in a loop: forward-values keeps one `mov $8, %edi` for both divides, and
// thread-jumps rotates the loop just before divide-by-constant reads the flow. Read stale, the flow
// said %edi was dead after the first divide; its rewrite used %edi and the second divided by garbage.
extern "C" int printf(const char *, ...);

static double m[8][8];
static long long w[4][4];

int main() {
    int i;
    for (i = 0; i < 64; ++i) m[i / 8][i % 8] = i * 0.5;
    for (i = 0; i < 16; ++i) w[i / 4][i % 4] = (long long)i * 3;
    unsigned u, s = 0;
    for (u = 0; u < 100; ++u) s += u / 7 * 10 + u % 7;
    printf("%.1f %.1f %.1f %lld %lld %u\n", m[0][1], m[5][3], m[7][7], w[2][3], w[3][3], s);
    return 0;
}
