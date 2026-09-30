// Division and remainder by constants the magic-number multiply treats apart:
// 1 and -1, negative divisors, powers of two either sign, INT_MIN, and
// unsigned divisors of 2^31 and above, over the edges of the range and a
// pseudo-random sweep. INT_MIN / -1 is left out: it is undefined behaviour.
// Every line is what clang prints.
extern "C" int printf(const char *, ...);

static int edges[] = { 0, 1, -1, 2, -2, 7, -7, 15, 16, 17, -15, -16, -17, 999, 1000, 1001, -1000,
                       2147483647, -2147483647, -2147483647 - 1, 1073741824, -1073741824, 123456789, -987654321 };
enum { ne = sizeof edges / sizeof edges[0] };
static unsigned uedges[] = { 0u, 1u, 2u, 15u, 16u, 17u, 0x7fffffffu, 0x80000000u, 0x80000001u,
                             0xfffffffeu, 0xffffffffu, 2999999999u, 3000000000u, 3000000001u, 123456789u };
enum { nu = sizeof uedges / sizeof uedges[0] };

#define S(D) printf("%d/%s=%d %%=%d  ", x, #D, x / (D), x % (D))
#define U(D) printf("%u/%s=%u %%=%u  ", x, #D, x / (D), x % (D))

static void signedRow(int x) {
    S(1); if (x != -2147483647 - 1) S(-1); S(2); S(-2); S(16); S(-16); S(3); S(-3); S(5); S(7); S(-7); S(10); S(-10); S(1000); S(-1000);
    S(641); S(-641); S(0x7fffffff); S(-0x7fffffff); S(1073741824); S(-1073741824); S(-2147483647 - 1);
    printf("\n");
}
static void unsignedRow(unsigned x) {
    U(1u); U(2u); U(16u); U(3u); U(5u); U(7u); U(10u); U(1000u); U(641u); U(0x7fffffffu);
    U(0x80000000u); U(0x80000001u); U(3000000000u); U(0xfffffffeu); U(0xffffffffu);
    printf("\n");
}

int main() {
    for (int i = 0; i < ne; ++i) signedRow(edges[i]);
    for (int i = 0; i < nu; ++i) unsignedRow(uedges[i]);
    unsigned seed = 12345u, sum = 0u;
    unsigned ssum = 0u;
    for (int i = 0; i < 4000; ++i) {   // a sweep: the sums would differ at the first wrong quotient
        seed = seed * 1103515245u + 12345u;
        unsigned u = seed ^ (seed >> 7);
        int v = (int)u;
        sum += u / 3u + u % 3u + u / 10u + u % 10u + u / 641u + u % 641u + u / 0x80000001u + u % 0xfffffffeu;
        ssum += (unsigned)(v / 3) + (unsigned)(v % 3) + (unsigned)(v / -7) + (unsigned)(v % -7) + (unsigned)(v / 1000)
              + (unsigned)(v % 1000) + (unsigned)(v / -16) + (unsigned)(v % -16) + (unsigned)(v / 0x7fffffff);
    }
    printf("sweep %u %u\n", sum, ssum);
    return 0;
}
