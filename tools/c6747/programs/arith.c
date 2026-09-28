/* Integer arithmetic the C6000 does in software or with care: division and
 * remainder of negatives, 64-bit multiply and divide, shifts, unsigned wrap. */
#include <stdio.h>

static int collatz(unsigned n)
{
    int steps = 0;
    while (n != 1) {
        n = (n & 1) ? 3 * n + 1 : n / 2;
        steps++;
    }
    return steps;
}

int main(void)
{
    int a = -17, b = 5;
    unsigned u = 0xFFFFFFF0u;
    long long big = 123456789LL * 1000003LL;
    int i, sum = 0;

    printf("div %d %d %d %d\n", a / b, a % b, -a / b, a / -b);
    printf("shift %d %u %x\n", a >> 2, u >> 4, (unsigned)a << 3);
    printf("wrap %u %u\n", u + 32u, 0u - 1u);
    printf("ll %lld %lld %lld\n", big, big / 977, big % 977);
    for (i = 1; i <= 100; i++)
        sum += i * i;
    printf("squares %d\n", sum);
    printf("collatz %d %d\n", collatz(27), collatz(97));
    return 0;
}
