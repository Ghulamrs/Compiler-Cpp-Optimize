// [dcl.link]/6: one function of a name has C linkage, and C++ overloads sit beside it.
// A redeclaration that leaves the linkage out keeps C's ([dcl.link]/5).
#include <stdio.h>
extern "C" int pick(int);
int pick(int);
long pick(long v) { return v * 10; }
double pick(double v) { return v / 2; }
extern "C" int pick(int n) { return n + 1; }
int main() {
    int (*c)(int) = pick;
    long (*l)(long) = pick;
    printf("%d %ld %g %d %ld\n", pick(3), pick(4L), pick(5.0), c(7), l(8L));
    return 0;
}
