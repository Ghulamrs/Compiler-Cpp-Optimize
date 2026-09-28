/* Single and double precision: the C674x has both in hardware, and printf's
 * conversion of them is the run-time library's. */
#include <stdio.h>

static double root(double x)
{
    double r = x / 2;
    int i;
    for (i = 0; i < 30; i++)
        r = (r + x / r) / 2;
    return r;
}

int main(void)
{
    float f = 1.0f / 3.0f;
    double d = 2.0 / 7.0;
    double acc = 0;
    int i;

    printf("third %.6f\n", f);
    printf("seventh %.10f\n", d);
    printf("root2 %.12f root10 %.12f\n", root(2.0), root(10.0));
    for (i = 1; i <= 1000; i++)
        acc += 1.0 / ((double)i * i);
    printf("basel %.9f\n", acc);
    printf("convert %d %d %u\n", (int)-2.75, (int)2.75, (unsigned)3e9);
    printf("sci %.4e %g\n", 6.02214076e23, 0.000125);
    return 0;
}
