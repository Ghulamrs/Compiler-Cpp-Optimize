// An 8-byte value assigned through an address whose computation calls a helper: i / 8 and i % 8
// call __c6xabi_divi and _remi on the C6000, and TI's clobber A5, so cpp11 -O0 saved only the low
// word and stored garbage high words. The VM6747 emulator's helpers are native and hid it (D2).
extern "C" int printf(const char *, ...);

static double m[8][8];
static long long w[4][4];

int main() {
    int i;
    for (i = 0; i < 64; ++i) m[i / 8][i % 8] = i * 0.5;
    for (i = 0; i < 16; ++i) w[i / 4][i % 4] = (long long)i * 1000000007LL;
    printf("%.1f %.1f %.1f %.1f\n", m[0][1], m[1][2], m[5][3], m[7][7]);
    printf("%lld %lld %lld\n", w[0][1], w[2][3], w[3][3]);
    return 0;
}
