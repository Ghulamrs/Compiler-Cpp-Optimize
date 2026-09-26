// Placement array new, `new (p) T[n]` - [expr.new]/12 for the reserved
// placement form: no cookie, so the array begins exactly where p points,
// whether or not T has a destructor. Measured from clang on both ABIs with a
// class carrying a destructor before this was written; a `new T[n]` of the
// same class puts a count in front, and the two are told apart here by the
// address the expression answers with.
//
// What a container's storage does with it: build n elements into raw bytes,
// take them apart with the explicit destructor call, and never `delete[]`
// the result - the ledger sees each built once and destroyed once.
#include "lifetime.h"
#include <new>

struct S {
    int v;
    S();
    ~S();
};
static int next = 1;
S::S() : v(next++) { lfBuilt(this, "S"); }
S::~S() { lfGone(this, "S"); }

struct Plain { int a; int b; };

static unsigned char raw[16 * sizeof(S)];
static unsigned char rawInts[16 * sizeof(int)];
static unsigned char rawPlain[16 * sizeof(Plain)];

int main() {
    lfWatch();
    S *s = new (raw) S[4];
    printf("at start %d\n", (void *)s == (void *)raw);
    printf("%d %d %d %d\n", s[0].v, s[1].v, s[2].v, s[3].v);
    for (int i = 3; i >= 0; i--) s[i].~S();

    int n = 5;
    S *t = new (raw) S[n];
    printf("%d %d\n", t[0].v, t[4].v);
    for (int i = n - 1; i >= 0; i--) t[i].~S();

    int *z = new (rawInts) int[6]();
    printf("zeroed %d %d %d\n", z[0], z[5], (void *)z == (void *)rawInts);

    Plain *p = new (rawPlain) Plain[3]();
    printf("plain %d %d %d\n", p[0].a, p[2].b, (void *)p == (void *)rawPlain);
    printf("end\n");
    return 0;
}
