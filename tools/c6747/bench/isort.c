/* Insertion sort: a data-dependent inner loop of loads, compares and stores. */
#include <stdio.h>
static unsigned seed = 12345;
static unsigned rnd(void) { seed = seed * 1103515245u + 12345u; return seed >> 8; }
static int sv[600];
static long long isort(int n)
{
    int i;
    long long s = 0;
    for (i = 0; i < n; ++i) sv[i] = (int)(rnd() % 100000);
    for (i = 1; i < n; ++i) {
        int x = sv[i], j = i - 1;
        while (j >= 0 && sv[j] > x) { sv[j + 1] = sv[j]; --j; }
        sv[j + 1] = x;
    }
    for (i = 0; i < n; i += 97) s += sv[i];
    return s;
}
int main(void) { printf("isort %lld\n", isort(600)); return 0; }
