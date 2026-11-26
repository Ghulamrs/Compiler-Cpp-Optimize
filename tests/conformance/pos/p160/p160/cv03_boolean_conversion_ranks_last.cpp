// [over.ics.rank]/4: a conversion that does not take a pointer or a pointer to member to bool is better than one
// that does - and with no other candidate the boolean conversion is a viable one.
#include <cstdio>
struct S { int x; };
struct D : S {};
static const char *h(bool) { return "bool"; }
static const char *h(void *) { return "void*"; }
static const char *j(bool) { return "bool"; }
static const char *j(int D::*) { return "member"; }
static const char *k(bool) { return "bool"; }
static const char *k(int) { return "int"; }
static const char *e(bool) { return "bool"; }
static const char *e(...) { return "ellipsis"; }
static int w(bool b) { return b ? 1 : 0; }
int f() { return 1; }
int main() {
    int t = 9;
    int *p = &t;
    int S::*q = &S::x;
    int arr[2];
    std::printf("%s %s %s %s\n", h(p), j(q), k(true), e(p));
    std::printf("%d %d %d %d\n", w(p), w(q), w(f), w(arr));
    return 0;
}
