// **What `operator new` still cannot be**: the array forms. `new T[n]` calls
// the platform's `operator new[]` and a class's plain `operator new` is not
// consulted for it, so a declared `operator new[]` could not be reached and
// is refused by name; operator-new.cpp is the rest, which works.
#include <stddef.h>
struct V {
    int x;
    static void *operator new[](size_t n);
};
int main(void) { return 0; }
