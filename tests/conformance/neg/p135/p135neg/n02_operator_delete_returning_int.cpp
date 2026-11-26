// [basic.stc.dynamic.deallocation]/2: a deallocation function returns void.
#include <cstddef>
struct S { static int operator delete(void *); };
int main() { return 0; }