// Loops that carry one value round - a sum, a dot product, Horner's rule, a running hash, a floating sum - which the
// C6000 pipeliner schedules at no less than the recurrence allows. A floating sum must add in the order written: the
// values are chosen so that any other order gives a different answer. Trip counts 0 to 17, 31 to 33, 64 and 257.
extern "C" int printf(const char *, ...);

static int ints[300], other[300];
static unsigned char bytes[300];
static short halves[300];
static double reals[300];
static float singles[300];

static unsigned sum(const int *a, int n) { unsigned s = 0; for (int i = 0; i < n; i++) s += a[i]; return s; }
static unsigned sumBytes(const unsigned char *a, int n) { unsigned s = 0; for (int i = 0; i < n; i++) s += a[i]; return s; }
static int sumHalves(const short *a, int n) { int s = 7; for (int i = 0; i < n; i++) s += a[i]; return s; }
static unsigned dot(const int *a, const int *b, int n) { unsigned s = 0; for (int i = 0; i < n; i++) s += (unsigned)a[i] * (unsigned)b[i]; return s; }
static unsigned horner(const int *a, int n, unsigned k) { unsigned x = 0; for (int i = 0; i < n; i++) x = x * k + (unsigned)a[i]; return x; }
static unsigned running(const unsigned char *a, int n) { unsigned h = 2166136261u; for (int i = 0; i < n; i++) h = (h ^ a[i]) * 16777619u; return h; }
static unsigned alternate(const int *a, int n) { unsigned s = 0, t = 0; for (int i = 0; i < n; i++) { s += (unsigned)a[i]; t ^= s; } return s * 3 + t; }
static double sumReals(const double *a, int n) { double s = 0; for (int i = 0; i < n; i++) s += a[i]; return s; }
static float sumSingles(const float *a, int n) { float s = 0; for (int i = 0; i < n; i++) s += a[i]; return s; }

int main() {
    for (int i = 0; i < 300; i++) {
        ints[i] = (i * 2654435761u) >> 7;
        other[i] = (int)((i * 40503u) % 1000) - 500;
        bytes[i] = (unsigned char)(i * 29 + 3);
        halves[i] = (short)(i * 977 - 30000);
        // Large and small in turn: reassociating changes which small ones survive.
        reals[i] = i % 3 == 0 ? 1.0e16 : i % 3 == 1 ? 1.0 : -1.0e16;
        singles[i] = i % 2 ? 3.0e7f : 1.0f;
    }
    static const int counts[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 31, 32, 33, 64, 257 };
    for (unsigned c = 0; c < sizeof counts / sizeof counts[0]; c++) {
        const int n = counts[c];
        printf("%3d: %u %u %d %u %u %u %u %.1f %.1f\n", n, sum(ints, n), sumBytes(bytes, n), sumHalves(halves, n),
               dot(ints, other, n), horner(other, n, 31), running(bytes, n), alternate(other, n),
               sumReals(reals, n), (double)sumSingles(singles, n));
    }
    return 0;
}
