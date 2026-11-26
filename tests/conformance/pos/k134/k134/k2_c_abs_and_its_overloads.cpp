// <stdlib.h>'s abs(int) keeps C linkage and <cstdlib> adds the overloads that answer
// in their argument's type; a runtime may define the C one itself.
#include <stdlib.h>
#include <cstdlib>
#include <stdio.h>
int main() {
    printf("%d %ld %lld %g %d\n", abs(-3), std::abs(-4L), std::abs(-5LL), std::abs(-2.5), std::abs(-6));
    printf("%d %d\n", (int)sizeof(std::abs(-4L)), (int)sizeof(std::abs(-5LL)));
    return 0;
}
