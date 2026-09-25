// [basic.stc.dynamic]/2: the replacement operator new is the one the whole
// program calls, so a static one at namespace scope is refused.
#include <stddef.h>
static void *operator new(size_t n);
int main() { return 0; }
