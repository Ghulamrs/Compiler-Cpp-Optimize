/* Structures, pointers, recursion, the string functions and qsort - the
 * library calls a small embedded program makes. Returns 7, so the exit
 * status each box reports is checked as well as the output. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct point { short x; int y; char tag; };

static int byY(const void *a, const void *b)
{
    const struct point *p = a, *q = b;
    return p->y - q->y;
}

static int fib(int n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }

int main(void)
{
    struct point pts[4] = { {1, 40, 'a'}, {2, -5, 'b'}, {3, 17, 'c'}, {4, 0, 'd'} };
    char buf[64];
    int i;

    qsort(pts, 4, sizeof pts[0], byY);
    for (i = 0; i < 4; i++)
        printf("%c(%d,%d) ", pts[i].tag, pts[i].x, pts[i].y);
    printf("\n");
    strcpy(buf, "C6747");
    strcat(buf, " says ");
    sprintf(buf + strlen(buf), "fib(20)=%d", fib(20));
    printf("%s [%u chars]\n", buf, (unsigned)strlen(buf));
    printf("cmp %d %d\n", strcmp("abc", "abd") < 0, memcmp("xy", "xy", 2));
    return 7;
}
