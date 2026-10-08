// A C++11 standard header include/ does not hold is refused by name - the
// header's own name and that it is not provided - where it had been "cannot
// find <tuple> - looked in ...". clang with libc++ or libstdc++ compiles this.
#include <tuple>

int main(void) { return 0; }
