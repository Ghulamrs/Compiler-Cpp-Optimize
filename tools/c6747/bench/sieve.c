/* Byte stores in a strided inner loop. */
#include <stdio.h>
static char comp[20001];
static int sieve(int n)
{
    int i, count = 0;
    long long j;
    for (i = 0; i <= n; ++i) comp[i] = 0;
    for (i = 2; i <= n; ++i) {
        if (comp[i]) continue;
        ++count;
        for (j = (long long)i * i; j <= n; j += i) comp[j] = 1;
    }
    return count;
}
int main(void) { int r, p = 0; for (r = 0; r < 2; ++r) p += sieve(20000); printf("sieve %d\n", p); return 0; }
