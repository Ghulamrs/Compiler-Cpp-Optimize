// [class.free]/2 with [expr.new]/9: the class's operator new hides the global placement forms.
#include <cstddef>
struct S { static void *operator new(std::size_t n); };
int main() { S *p = new (1) S; (void)p; return 0; }