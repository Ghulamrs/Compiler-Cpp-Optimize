// [expr.add]/6: a difference of two pointers ignores their pointees' const, and
// [expr.rel]/[expr.eq] compare T * with const T * at the composite pointer type.
extern "C" int printf(const char *, ...);

struct P { int a; double b; };

int main() {
    char buf[8] = "abcdefg";
    char *p = buf + 5;
    const char *q = buf + 2;
    printf("%d %d\n", (int)(p - q), (int)(q - p));
    printf("%d %d %d %d\n", p < q, q < p, p <= q, q <= p);
    printf("%d %d %d %d\n", p > q, q > p, p >= q, q >= p);
    printf("%d %d %d %d\n", p == q, q == p, p != q, q != p);
    const char *r = buf + 5;
    printf("%d %d %d %d\n", (int)(p - r), p == r, r <= p, p != r);

    int a[6] = {0, 1, 2, 3, 4, 5};
    int *ip = a + 4;
    const int *cip = a + 1;
    printf("%d %d %d %d\n", (int)(ip - cip), (int)(cip - ip), cip < ip, ip == cip);

    P ps[4];
    const P *cp = ps;
    P *mp = ps + 3;
    printf("%d %d %d\n", (int)(mp - cp), (int)(cp - mp), cp != mp);

    const char *const cc = buf + 7;
    printf("%d %d\n", (int)(cc - p), (int)(p - cc));
    return 0;
}
