/* Double-precision multiply-add over three 24x24 arrays. */
#include <stdio.h>
static double ma[24][24], mb[24][24], mc[24][24];
static double matmul(int n)
{
    int i, j, k;
    double s = 0;
    for (i = 0; i < n; ++i)
        for (j = 0; j < n; ++j) { ma[i][j] = (i + j) % 7; mb[i][j] = (i * j) % 5; mc[i][j] = 0; }
    for (i = 0; i < n; ++i)
        for (k = 0; k < n; ++k) {
            double x = ma[i][k];
            for (j = 0; j < n; ++j) mc[i][j] += x * mb[k][j];
        }
    for (i = 0; i < n; ++i) for (j = 0; j < n; ++j) s += mc[i][j];
    return s;
}
int main(void) { printf("matmul %.0f\n", matmul(24)); return 0; }
